CC=gcc
CFLAGS= -Wall

minishell: minishell.o cmdparse.o
	${CC} ${CFLAGS} -o myshell minishell.o cmdparse.o

minishell.o: minishell.c cmdparse.h
	${CC} ${CFLAGS} -o minishell.o -c minishell.c

cmdparse.o: cmdparse.c cmdparse.h
	${CC} ${CFLAGS} -o cmdparse.o -c cmdparse.c

clean:
	rm -f myshell minishell.o cmdparse.o
