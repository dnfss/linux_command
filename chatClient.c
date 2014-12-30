
/**
  * Note
  *		1. POLLRDHUP
  *			Stream socket peer closed connection, or shut down writing half of connection.
  *			The _GNU_SOURCE feature test macro must be defined (before including any header files) in order to obtain this definition.
  * 	2. zero-copy: splice, sendfile
  * 	http://stackoverflow.com/questions/8626263/understanding-sendfile-and-splice
  */

#define _GNU_SOURCE 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

const int BUFFER_SIZE = 64;

int main(int argc, char *argv[]) {
	if( argc < 2 ) {
		printf("usage: %s ip port\n", basename(argv[0]));
		return -1;
	}

	struct sockaddr_in svrAddr;
	bzero(&svrAddr, sizeof(svrAddr));
	svrAddr.sin_family = AF_INET;
	svrAddr.sin_port = htons(atoi(argv[2]));
	inet_pton(AF_INET, argv[1], &svrAddr.sin_addr);

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if( sockfd < 0 ) {
		printf("ERR! sock ret[%d]\n", sockfd);
		return -2;
	}

	int ret;
	if( (ret = connect(sockfd, (struct sockaddr*)&svrAddr, sizeof(svrAddr))) < 0 ){
		printf("ERR! connect ret[%d], errno[%d]\n", ret, errno);
		return -3;
	}

	struct pollfd fds[2];
	fds[0].fd = 0;
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	fds[1].fd = sockfd;
	fds[1].events = POLLIN | POLLRDHUP;
	fds[1].revents = 0;

	char readBuf[BUFFER_SIZE];

	int pipefd[2];
	if( (ret = pipe(pipefd)) == -1 ) {
		printf("ERR! pipe ret[%d]\n", ret);
		return -5;
	}

	while( 1 ) {
		if( (ret = poll(fds, 2, -1 )) < 0 ) {
			printf("ERR! poll ret[%d]\n", ret);
			return -4;
		}

		if( fds[1].revents & POLLRDHUP ) {
			printf("server close connection\n");
			break;
		}
		else if( fds[1].revents & POLLIN ) {
			memset(readBuf, 0, sizeof(readBuf));
			recv(fds[1].fd, readBuf, sizeof(readBuf) - 1, 0);
			printf("%s\n", readBuf);
		}

		if( fds[0].revents & POLLIN ) {
			ret = splice(0, NULL, pipefd[1], NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
			ret = splice(pipefd[0], NULL, sockfd, NULL, 32768, SPLICE_F_MORE | SPLICE_F_MOVE);
		}
	}

	return 0;
}
