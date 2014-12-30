
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

#define USER_LIMIT 5
#define FD_LIMIT 65535
#define READ_BUFFER_SIZE 64

struct clientData {
	sockaddr_in addr;
	char *writeBuf;
	char buf[READ_BUFFER_SIZE];
};

int setnonblock(int fd) {
	int old = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, old | O_NONBLOCK);
	return old;
}

int main(int argc, char *argv[]) {
	if( argc < 2 ) {
		printf("usage: %s ip port\n", basename(argv[0]));
		return -1;
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[2]));
	inet_pton(AF_INET, argv[1], &addr.sin_addr);

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if( listenfd < 0 ) {
		printf("ERR! sock ret[%d]\n", listenfd);
		return -2;
	}

	int ret = bind(listenfd, (struct sockaddr*)&addr, sizeof(addr));
	if( ret < 0 ) {
		 printf("ERR! bind ret[%d]\n", ret);
		 return -3;
	}

	if( (ret = listen(listenfd, 5)) < 0 ) {
		printf("ERR! listen ret[%d]\n", ret);
		return -4;
	}

	int i;
	struct pollfd fds[USER_LIMIT + 1];
	for(i = 1; i <= USER_LIMIT; ++i) {
		fds[i].fd = -1;
		fds[i].events = 0;
	}
	fds[0].fd = listenfd;
	fds[0].events = POLLIN | POLLERR;
	fds[0].revents = 0;

	int userCnt = 0;
	struct clientData *users = (struct clientData*)malloc(sizeof(struct clientData) * FD_LIMIT);
	while( 1 ) {
		if( (ret = poll(fds, userCnt + 1, -1)) < 0 ) {
			printf("ERR! poll ret[%d]\n", ret);
			return -5;
		}
		printf("poll ret[%d]\n", ret);

		for(i = 0; i < userCnt + 1; ++i) {
			if( fds[i].fd == listenfd && fds[i].revents & POLLIN ) {
				struct sockaddr_in clientAddr;
				socklen_t clientAddrLen = sizeof(clientAddr);
				
				int connfd = accept(listenfd, (struct sockaddr*)&clientAddr, &clientAddrLen);
				if( connfd < 0 ) {
					printf("ERR! accept ret[%d], errno[%d]\n", ret, errno);
					continue;
				}

				if( userCnt >= USER_LIMIT ) {	// too much request, deny
					const char *info = "too much users\n";
					printf("%s", info);
					send(connfd, info, strlen(info), 0);
					close(connfd);
					continue;
				}

				setnonblock(connfd);

				users[connfd].addr = clientAddr;
				fds[++userCnt].fd = connfd;
				fds[userCnt].events = POLLIN | POLLRDHUP | POLLERR;
				fds[userCnt].revents = 0;
				printf("comes a new user, now have %d user\n", userCnt);
			}
			else if( fds[i].revents & POLLERR ) {
				printf("get an error from %d\n", fds[i].fd);
				
				char buf[256] = {0};
				socklen_t bufLen = sizeof(buf);
				if( (ret = getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, &buf, &bufLen)) < 0 ) {
					printf("ERR! getsockopt ret[%d]\n", ret);
					continue;
				}
				printf("reason: %s\n", buf);
			}
			else if( fds[i].revents & POLLRDHUP ) {
				users[fds[i].fd] = users[fds[userCnt].fd];
				close(fds[i].fd);
				fds[i--] = fds[userCnt--];
				printf("a client left\n");
			}
			else if( fds[i].revents & POLLIN ) {
				int tmpfd = fds[i].fd;
				memset(users[tmpfd].buf, 0, sizeof(users[tmpfd].buf));

				ret = recv(tmpfd, users[tmpfd].buf, sizeof(users[tmpfd].buf) - 1, 0);
				printf("get %d bytes from fd %d, content[%s]\n", ret, tmpfd, users[tmpfd].buf);

				if( ret < 0 ) {
					if( errno != EAGAIN ) {
						close(tmpfd);
						users[fds[i].fd] = users[fds[userCnt].fd];
						fds[i--] = fds[userCnt--];
					}
				}
				else if( ret > 0 ) {
					int j;
					for(j = 1; j <= userCnt; ++j) {
						if( fds[j].fd == listenfd ) {
							continue;
						}

						fds[j].events |= ~POLLIN;
						fds[j].events |= POLLOUT;
						users[fds[j].fd].writeBuf = users[tmpfd].buf;
					}
				}
			}
			else if( fds[i].revents & POLLOUT ) {
				int tmpfd = fds[i].fd;
				if( users[tmpfd].writeBuf == NULL ) {
					continue;
				}

				ret = send(tmpfd, users[tmpfd].writeBuf, strlen(users[tmpfd].writeBuf), 0);
				users[tmpfd].writeBuf = NULL;
				fds[i].events |= POLLIN;
				fds[i].events |= ~POLLOUT;
			}
		}
	}

	free(users);
	close(listenfd);
	return 0;
}

