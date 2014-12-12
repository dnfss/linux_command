
/*
 * Note:
 *	creat() is equivalent to open() with flags equal to O_CREAT|O_WRONLY|O_TRUNC
 *	how to choose to buffersize to get the best performance
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define BUFFERSIZE 4096
#define COPYMODE 0644

int main(int argc, char *argv[]) {
	if( argc != 3 ) {
		fprintf(stderr, "usage: %s source destination\n", argv[0]);
		exit(-1);
	}

	int inFd = open(argv[1], O_RDONLY);
	if( inFd == -1 ) {
		fprintf(stderr, "can not open %s\n", argv[1]);
		exit(-1);
	}

	int outFd = open(argv[2], O_CREAT|O_WRONLY|O_TRUNC, COPYMODE);
	if( outFd == -1 ) {
		fprintf(stderr, "can not create %s\n", argv[2]);
		close(inFd);
		exit(-1);
	}

	int readCnt;
	char buf[BUFFERSIZE];
	while( (readCnt = read(inFd, buf, BUFFERSIZE)) > 0 ) {
		if( write(outFd, buf, readCnt) != readCnt ) {
			fprintf(stderr, "write error to %s", argv[2]);
			close(inFd);
			close(outFd);
			exit(-1);
		}
	}

	if( readCnt == -1 ) {
		fprintf(stderr, "read error from %s\n", argv[1]);
		close(inFd);
		close(outFd);
		exit(-1);
	}

	close(inFd);
	close(outFd);
	return 0;
}
