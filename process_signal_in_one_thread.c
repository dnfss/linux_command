
/**
  * Note
  * 	1. use this techique if process signal in multi-thread progam
  */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#define HANDLE_ERROR_EN(en, msg) \
	do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

static void *sigThread(void *arg) {
	int s, sig;
	sigset_t *set = (sigset_t*)arg;
	while( 1 ) {
		s = sigwait(set, &sig);
		if( s!= 0 ) {
			HANDLE_ERROR_EN(s, "sigwait");
		}
		printf("Signal handling thread got signal %d\n", sig);
	}
}

int main(int argc, char *argv[]) {
	sigset_t set;
	pthread_t thread;

	sigemptyset(&set);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGUSR1);
	sigaddset(&set, SIGINT);
	int s = pthread_sigmask(SIG_BLOCK, &set, NULL);
	if( s != 0 ) {
		HANDLE_ERROR_EN(s, "pthread_sigmask");
	}

	if( (s = pthread_create(&thread, NULL, &sigThread, (void*)&set)) != 0 ) {
		HANDLE_ERROR_EN(s, "pthread_create");
	}

	pause();
	return 0;
}

