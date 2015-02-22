
/**
  * Note
  *
  */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/epoll.h>

#include "CLocker.h"
#include "CThreadPool.h"
#include "CHttpConnection.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10240;

extern int AddFd(int epollfd, int fd, bool oneShot);
extern int RemoveFd(int epollfd, int fd);

void AddSig(int sig, void (*handler)(int), bool restart = true) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if( restart ) {
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

void ShowError(int connfd, const char *info) {
	printf("%s", info);
	send(connfd, info, strlen(info), 0);
	close(connfd);
}

int main(int argc, char *argv[]) {
	if( argc != 3 ) {
		printf("usage: %s ip port\n", basename(argv[0]));
		return -1;
	}

	AddSig(SIGPIPE, SIG_IGN);

	CThreadPool<CHttpConnection> *pool = NULL;
	try {
		pool = new CThreadPool<CHttpConnection>;
	}
	catch(...) {
		printf("create thread pool error\n");
		return -1;
	}

	CHttpConnection *users = new CHttpConnection[MAX_FD];
	assert(users);

	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	assert(listenfd >= 0);
	struct linger tmp = {1, 0};
	setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

	int ret = 0;
	struct sockaddr_in addr;
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_pton(AF_INET, argv[1], &addr.sin_addr);
	addr.sin_port = htons(atoi(argv[2]));
	assert(bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) >= 0);
	assert(listen(listenfd, 5) >= 0);

	epoll_event events[MAX_EVENT_NUMBER];
	int epollfd = epoll_create(5);
	assert(epollfd != -1);
	AddFd(epollfd, listenfd, false);
	CHttpConnection::m_epollfd = epollfd;

	while( true ) {
		int cnt = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if( cnt < 0 && errnor != EINTR ) {
			printf("epoll failure\n");
			break;
		}

		for(int i = 0; i < cnt; ++i) {
			int fd = events[i].data.fd;
			if( fd == listenfd ) {
				struct sockaddr_in clientAddr;
				socklen_t clientAddrLen = sizeof(clientAddr);
				int connfd = accept(listenfd, (struct sockaddr*)&clientAddr, &clientAddrLen);
				if( connfd < 0 ) {
					printf("accept error[%d]\n", errno);
					continue;
				}
				if( CHttpConnection::m_userCnt >= MAX_FD ) {
					ShowError(connfd, "Internal server busy");
					continue;
				}
				users[connfd].init(connfd, clientAddr);
			}
			else if( events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR) ) {
				users[fd].CloseConn();
			}
			else if( events[i].events & EPOLLIN ) {
				if( users[fd].Read() ) {
					pool->Append(users + fd);
				}
				else {
					users[fd].CloseConn();
				}
			}
			else if( events[i].events & EPOLLOUT ) {
				if( !users[fd].Write() ) {
					users[fd].CloseConn();
				}
			}
		}
	}

	close(epollfd);
	close(listenfd);
	delete[] users;
	delete pool;
	return 0;
}
