
/*
 * Note
 *	   1. This is an echo server which process tcp and udp
 *     2. serve multi users by one process which employ epoll
 *     3. demonstrate how to deal with signal with Unified Event Model
 *     4. demonstrate how to deal with inactive connection
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "CWheelTimer.h"

const int TIME_SLOT = 5;
const int FD_LIMIT = 65535;
const int MAX_EVENT_NUMBER = 1024;
const int TCP_BUFFER_SIZE = 512;
const int UDP_BUFFER_SIZE = 512;

static int pipefd[2];

struct Data {
	Data(int tmpFd):fd(tmpFd) {}

	int fd;
};

int setnonblock(int fd) {
	int old = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, old | O_NONBLOCK);
	return old;
}

void addfd(int epollfd, int fd) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblock(fd);
}

int createSvrSocket(const char *ip, int port, int socketType) {
	int sockfd;
	if( socketType == 1 ) {	// TCP
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
	}
	else {	// UDP
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	}
	if( sockfd < 0 ) {
		printf("<createSvrSocket> ERR! sock ret[%d]\n", sockfd);
		return -2;
	}

	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &addr.sin_addr);

	int ret = bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
	if( ret < 0 ) {
		 printf("<createSvrSocket> ERR! bind ret[%d]\n", ret);
		 return -3;
	}

	if( socketType == 1 ) {
		if( (ret = listen(sockfd, 5)) < 0 ) {
			printf("<createSvrSocket> ERR! listen ret[%d]\n", ret);
			return -4;
		}
	}

	return sockfd;
}

void sig_handler(int sig) {
	int save_errno = errno;
	int msg = sig;

	// notify the main loop
	send(pipefd[1], (char*)&msg, 1, 0);
	errno = save_errno;
}

void addsig(int sig) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);

	int ret;
	if( (ret = sigaction(sig, &sa, NULL)) < 0 ) {
		printf("<addsig> ERR! sigaction ret[%d]\n", ret);
		exit(-1);
	}
}

void close_inactive_conn(void *data) {
	Data *curData = (Data*)data;
	printf("close timeout conn fd[%d]\n", curData->fd);
	close(curData->fd);
	delete curData;
}

int main(int argc, char *argv[]) {
	if( argc < 2 ) {
		printf("usage: %s ip port\n", basename(argv[0]));
		return -1;
	}

	struct epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);
	if( epollfd == -1 ) {
		printf("epoll_create ret[%d], errno[%d]\n", epollfd, errno);
		return -2;
	}

	int tcpfd = createSvrSocket(argv[1], atoi(argv[2]), 1);
	addfd(epollfd, tcpfd);

	int udpfd = createSvrSocket(argv[1], atoi(argv[2]), 2);
	addfd(epollfd, udpfd);

	//build the pipe
	int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
	if( ret < 0 ) {
		printf("socketpair ret[%d]\n", ret);
		return -3;
	}
	setnonblock(pipefd[1]);
	addfd(epollfd, pipefd[0]);

	addsig(SIGHUP);
	addsig(SIGCHLD);
	addsig(SIGTERM);
	addsig(SIGINT);
	addsig(SIGALRM);

	CWheelTimer timer;
	WheelTimerNode *timerNodes[FD_LIMIT];
	alarm(TIME_SLOT);

	int stopSvr = 0;
	while( !stopSvr ) {
		int cnt = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if( cnt < 0 && (errno != EINTR) ) {	// check errno as epoll_wait would be interrupted by signal
			 printf("epoll_wait ret[%d]\n", cnt);
			 return -4;
		}

		int i, ret;
		for(i = 0; i < cnt; ++i) {
			int sockfd = events[i].data.fd;
			if( sockfd == tcpfd ) {
				struct sockaddr_in clientAddr;
				socklen_t clientAddrLen = sizeof(clientAddr);
				int connfd = accept(sockfd, (struct sockaddr*)&clientAddr, &clientAddrLen);
				addfd(epollfd, connfd);

				// Note: important to use dynamic allocation
				Data *data = new Data(connfd);
				timerNodes[connfd] = timer.AddTimer(1, close_inactive_conn, data);
			}
			else if( sockfd == udpfd ) {
				char buf[UDP_BUFFER_SIZE] = {0};
				struct sockaddr_in clientAddr;
				socklen_t clientAddrLen = sizeof(clientAddr);
				ret = recvfrom(sockfd, buf, UDP_BUFFER_SIZE - 1, 0,
						(struct sockaddr*)&clientAddr, &clientAddrLen);
				if( ret > 0 ) {
					sendto(sockfd, buf, UDP_BUFFER_SIZE - 1, 0,
							(struct sockaddr*)&clientAddr, clientAddrLen);
				}
			}
			else if( sockfd == pipefd[0] ) {
				char strSignal[1024] = {0};
				ret = recv(pipefd[0], strSignal, sizeof(strSignal), 0);
				if( ret <= 0 ) {
					continue;
				}
				else {
					int j;
					for(j = 0; j < ret; ++j) {	// one signal, one byte
						switch(strSignal[j]) {
							case SIGALRM:
								timer.Tick();
								alarm(TIME_SLOT);
								printf("tick\n");
								break;
							case SIGCHLD:
							case SIGHUP:
								printf("get signal[%d]\n", (int)strSignal[j]);
								break;
							case SIGTERM:
							case SIGINT:
								printf("get signal[%d], need to stop\n", (int)strSignal[j]);
								stopSvr = 1;
								break;
						}
					}
				}
			}
			else if( events[i].events & EPOLLIN ) {
				char buf[TCP_BUFFER_SIZE] = {0};
				while( 1 ) {
					ret = recv(sockfd, buf, TCP_BUFFER_SIZE - 1, 0);
					if( ret < 0 ) {
						if( errno == EAGAIN || errno == EWOULDBLOCK ) {	// break as we serve multi users by one process
							break;
						}

						close(sockfd);
						break;
					}
					else if( ret == 0 || strcasecmp("exit\r\n", buf) == 0 ) {
						close(sockfd);
					}
					else {
						send(sockfd, buf, ret, 0);
						printf("renew fd[%d]\n", sockfd);

						Data *data = (Data*)(timerNodes[sockfd]->userData);
						timer.DelTimer(timerNodes[sockfd]);
						delete data;
						data = new Data(sockfd);
						timerNodes[sockfd]= timer.AddTimer(1, close_inactive_conn, data);
					}
					memset(buf, 0, sizeof(buf));
				}
			}
			else {
				printf("something else happened\n");
			}
		}
	}

	close(tcpfd);
	close(pipefd[0]);
	close(pipefd[1]);
	return 0;
}
