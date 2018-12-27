#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>
#include <termios.h>
#include "cmdparse.h"
#include "minishell.h"

/* Determines the mode to use when using output redirection */
#define OUTPUT_MODE(n) ((n) ? O_WRONLY|O_APPEND|O_CREAT : O_TRUNC|O_WRONLY|O_CREAT )

#define READ  0
#define WRITE 1
#define MAX_BUF 300
#define MAX_CWD 200
#define DEL 127
#define PROMPTLEN 3

char           cwd[MAX_CWD];
struct termios oldTerminal;

/*
 * This is a simple shell application that supports some common
 * shell actions such as input and output redirection, pipes, and
 * backgrounding.
 */
int main()
{
	CMD  command;
	char buf[MAX_BUF];
	char c;
	int  n;
	int  cursor;
	int  startOfCmd;

	if (goNonCanonical() < 0) {
		printf("Could not setup terminal\n");
		return 1;
	}

	startOfCmd = setupPrompt(buf);
	cursor = startOfCmd;
	while (read(STDIN_FILENO, &c, 1) == 1) {
		switch (c) {
			case DEL:
				if (cursor > startOfCmd) {
					buf[cursor] = '\0';
					cursor--;
					char delbuf[] = "\b \b";
					write(STDOUT_FILENO, delbuf, strlen(delbuf));
				}
				break;
			case '\n':
				printf("\n");
				n = cmdparse(&buf[startOfCmd], &command);
				if (n == PARSE_ERROR) {
 					fprintf(stderr, "parse error\n");
					continue;
				} else if (n == NO_INPUT) {
					continue;
				} else if (strcmp(command.argv1[0], "exit") == 0) {
					goCanonical();
					return 0;
				}
				runCommand(command);
				startOfCmd = setupPrompt(buf);
				cursor = startOfCmd;
				break;
			case '\t':
				cursor += tabComplete(&buf[startOfCmd]);
				break;
			default:
				buf[cursor++] = c;
				write(STDOUT_FILENO, &c, sizeof(c));
		}
	}

	return 0;
}

/*
 * Switches the terminal IO to noncanonical mode for
 * command input processing, returns -1 if there is
 * an error, or 0 of successful
 */
int goNonCanonical()
{
	struct termios term;

	if (tcgetattr(STDIN_FILENO, &term) < 0)
		return -1;
	oldTerminal = term;

	/* Config the new terminal mode */
	term.c_lflag &= ~(ECHO | ICANON);
	term.c_cc[VMIN] = 1;
	term.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0)
		return -1;

	/* Check that all terminal changes were made */
	if (tcgetattr(STDIN_FILENO, &term) < 0) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldTerminal); // restore old terminal
		return -1;
	}
	if ((term.c_lflag & (ECHO | ICANON)) || term.c_cc[VMIN] != 1 || term.c_cc[VTIME] != 0) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldTerminal);
		return -1;
	}

	return 0;
}

/*
 * Switches the terminal IO back to canonical mode upon exit
 */
void goCanonical()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &oldTerminal);
}

/*
 * Creates the command prompt that will contain the
 * full path to the current directory and a '$'
 */
int setupPrompt(char *buf)
{
	int  startOfCmd;
	char tempCwd[MAX_CWD];

	memset(buf, '\0', MAX_BUF);
	getcwd(cwd, MAX_CWD);
	strcpy(tempCwd, cwd);
	startOfCmd = strlen(cwd) + PROMPTLEN + 1; // 1 for a space after prompt
	strcat(tempCwd, " $ ");
	strcpy(buf, tempCwd);
	write(STDOUT_FILENO, buf, strlen(buf));
	return startOfCmd;
}

/*
 * Attempts to autocomplete the file name or directory
 * name that user started to type. It will return the
 * number characters added to complete the name.
 */
int tabComplete(char *buf)
{
	int           ndx;
	int           len;
	int           matchingFiles;
	int           matchFound;
	int           charsAdded;
	char          *token;
	char          *finalToken;
	char          bufCpy[strlen(buf)+1];
	char          completedFile[100];
	DIR           *dir;
	struct dirent *currFile;

	/* Copy the buf for tokenizing */
	strcpy(bufCpy, buf);

	/* Get the last token */
	finalToken = NULL;
	token = strtok(bufCpy, " ");
	while (token != NULL) {
		finalToken = token;
		token = strtok(NULL, " ");
	}

	if ((dir = opendir(cwd)) == NULL) {
		printf("tabComplete: could not read directory: %s\n", strerror(errno));
		return 0;
	}

	charsAdded = 0;
	matchingFiles = 0;
	while ((currFile = readdir(dir)) != NULL) {
		// set loop counter to the smallest string length
		if (strlen(currFile->d_name) < strlen(finalToken))
			len = strlen(currFile->d_name);
		else
			len = strlen(finalToken);

		matchFound = 1;
		for (ndx = 0; ndx < len; ndx++) {
			if (currFile->d_name[ndx] != finalToken[ndx]) {
				matchFound = 0;
				break;
			}
		}

		if (matchFound) {
			strcpy(completedFile, currFile->d_name);
			matchingFiles++;
		}
	}

	// complete nothing if more than one file can match
	if (matchingFiles == 1) {
		strcat(buf, &completedFile[strlen(finalToken)]);
		write(STDOUT_FILENO, &completedFile[strlen(finalToken)], strlen(completedFile));
		charsAdded = strlen(&completedFile[strlen(finalToken)]);
	}

	closedir(dir);
	return charsAdded;
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
