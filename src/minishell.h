#ifndef _MINISHELL_
#define _MINISHELL_

int  goNonCanonical();
void goCanonical();
int  setupPrompt();
int  tabComplete(char *buf);
void runCommand(CMD command);
void doubleFork(CMD command, int pipeFd[2], int forCmd1);
void singleFork(CMD command, int pipeFd[2]);
void secondFork(long forkPid, CMD command, int pipeFd[2], int forCmd1);
void child(CMD command, int pipeFd[2], int isPipeWriter);
void processPipe(int pipeFd[2], int isPipeWriter);
void redirectInput(CMD command);
void redirectOutput(CMD command);

#endif
