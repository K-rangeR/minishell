#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include "cmdparse.h"

/* Determines the mode to use when using output redirection */
#define OUTPUT_MODE(n) ((n) ? O_WRONLY|O_APPEND|O_CREAT : O_TRUNC|O_WRONLY|O_CREAT )

/* index into the pipe array */
#define READ  0
#define WRITE 1

#define MAX_BUF 101
#define MAX_CWD 200

/* Helper functions */
void readCharByChar(char *buf);
void tabComplete(char *buf);
void runCommand(CMD command);
void doubleFork(CMD command, int pipeFd[2], int forCmd1);
void singleFork(CMD command, int pipeFd[2]);
void secondFork(long forkPid, CMD command, int pipeFd[2], int forCmd1);
void child(CMD command, int pipeFd[2], int isPipeWriter);
void processPipe(int pipeFd[2], int isPipeWriter);
void redirectInput(CMD command);
void redirectOutput(CMD command);

/*
 * This is a simple shell application that supports some common
 * shell actions such as input and output redirection, pipes, and
 * backgrounding.
 */
int main()
{
	CMD  command;
	char buf[MAX_BUF];
	char cwd[MAX_CWD];
	int  n;

	while (1) {
		getcwd(cwd, MAX_CWD);
		printf("%s $ ", cwd);
		readCharByChar(buf);
		n = cmdparse(buf, &command);

		if (n == PARSE_ERROR) {
 			fprintf(stderr, "parse error\n");
			continue;
		} else if (n == NO_INPUT) {
			continue;
		} else if (strcmp(command.argv1[0], "exit") == 0) {
			break;
		}

		runCommand(command);
	}

	return 0;
}

void readCharByChar(char *buf)
{
	int c;
	int n;
	
	n = 0;
	while ((c = fgetc(stdin)) != '\n') {
		if (c == '\t')
			tabComplete(buf);
		else
			buf[n++] = c;
	}

	buf[n] = '\0';
}

void tabComplete(char *buf)
{

}

/*
 * Starts the execution of the commands
 */
void runCommand(CMD command)
{
	int  pipeFd[2];

	if (strcmp(command.argv1[0], "cd") == 0) {
		if (chdir(command.argv1[1]) < 0)
			printf("runCommand: could not change directory: %s\n", strerror(errno));
		return;
	}

	if (command.pipelining) {
		if (pipe(pipeFd) == -1) {
			fprintf(stderr, "runCommand: could not create the pipe: %s\n", strerror(errno));
			return;
		}
	}

	if (command.background)
		doubleFork(command, pipeFd, TRUE);
	else
		singleFork(command, pipeFd);
}

/*
 * Performs a double fork of the 2 child processes so the parent
 * can run the both of them in the background (not wait on them)
 * and not have to worry about them becoming zombies. Instead the
 * 2 children are adopted by init.
 */
void doubleFork(CMD command, int pipeFd[2], int forCmd1)
{
	long forkPid;
	int  exitStatus;

	forkPid = fork();
	switch (forkPid) {
		case -1:
			fprintf(stderr, "parent: could not run command '%s': %s\n", 
					command.argv2[0], strerror(errno));
			exit(1);
		case 0:
			forkPid = fork();
			secondFork(forkPid, command, pipeFd, forCmd1);	
		default:
			if (!forCmd1)
				close(pipeFd[0]); close(pipeFd[1]);

			if (waitpid(forkPid, &exitStatus, 0) == -1)
				fprintf(stderr, "doubleFork: could not wait on child: %s\n", strerror(errno));

			if (exitStatus != 0) {
				fprintf(stderr, "doubleFork: first child error: %d\n", exitStatus);
				return;
			}

			if (command.pipelining && forCmd1)
				doubleFork(command, pipeFd, FALSE);
	}
}

/*
 * Runs the logic for when the grandchild is forked, this is the
 * child that will run the command
 */
void secondFork(long forkPid, CMD command, int pipeFd[2], int forCmd1)
{
	switch (forkPid) {
		case -1:
			fprintf(stderr, "doubleFork: second fork fail: %s\n", strerror(errno));
			exit(1);
		case 0:
			child(command, pipeFd, forCmd1);	
		default:
			exit(0);
	}
}

/*
 * Only forks each child one time because they are not running
 * the background so the parent must wait on them
 */
void singleFork(CMD command, int pipeFd[2])
{
	long forkPid1;
	long forkPid2;

	forkPid1 = fork();
	switch (forkPid1) {
		case -1:
			fprintf(stderr, "singleFork: could not run command: %s\n", strerror(errno));
			return;
		case 0:
			child(command, pipeFd, TRUE);
		default:
			if (command.pipelining) {
				forkPid2 = fork();
				switch (forkPid2) {
					case -1:
						fprintf(stderr, "parent: could not run command: %s\n", 
							 		strerror(errno));
					break;
				case 0:
					child(command, pipeFd, FALSE);
					break;
				default:
					close(pipeFd[0]); close(pipeFd[1]);
					if (waitpid(forkPid2, 0, 0) == -1)
						fprintf(stderr, "singleFork: could not wait on child: %s\n", 
									strerror(errno));
					fflush(stderr);
				}
			}
	}
	if (waitpid(forkPid1, 0, 0) == -1)
		fprintf(stderr, "singleFork: could not wait on child: %s\n", strerror(errno));
}

/*
 * Main function run by each child process
 */
void child(CMD command, int pipeFd[2], int isPipeWriter)
{
	if (command.pipelining)
		processPipe(pipeFd, isPipeWriter);	

	if (command.redirectIn && isPipeWriter)
		redirectInput(command);
	
	if ((command.redirectOut && !isPipeWriter) || (command.redirectOut && !command.pipelining))
		redirectOutput(command);

	if (isPipeWriter)
		execvp(command.argv1[0], command.argv1);
	else
		execvp(command.argv2[0], command.argv2);

	fprintf(stderr, "child: could not run command '%s': %s\n", command.argv1[0], strerror(errno));
	exit(1);
}

/*
 * Determines which file descriptor to redirect for the pipe
 * based on if the process calling this function is the writer
 * of the pipe or the reader of the pipe
 */
void processPipe(int pipeFd[2], int isPipeWriter)
{
	int source;
	int change;

	if (isPipeWriter) {
		source = pipeFd[WRITE];
		change = STDOUT_FILENO;
	} else {
		source = pipeFd[READ];
		change = STDIN_FILENO;
	}

	if (dup2(source, change) == -1)
		fprintf(stderr, "processPipe: could not redirect descriptors: %s\n", strerror(errno));

	close(pipeFd[0]); close(pipeFd[1]);
}

/*
 * Handles redirecting the input from the input file
 */
void redirectInput(CMD command)
{
	int fd;

	if ((fd = open(command.infile, O_RDONLY, 0)) < 0) {
		fprintf(stderr, "could not open the input file: %s\n", strerror(errno));	
		exit(1);
	}
	if (dup2(fd, STDIN_FILENO) == -1) {
		fprintf(stderr, "could not redirect input: %s\n", strerror(errno));
		exit(1);
	}
}

/*
 * Handles redirecting the output to the output file
 */
void redirectOutput(CMD command)
{
	int fd;

	if ((fd = open(command.outfile, OUTPUT_MODE(command.redirectAppend), 0644)) < 0) {
		fprintf(stderr, "could not open the output file: %s\n", strerror(errno));
		exit(1);
	}
	if (dup2(fd, STDOUT_FILENO) == -1) {
		fprintf(stderr, "could not redirect output: %s\n", strerror(errno));
		exit(1);
	}
}
