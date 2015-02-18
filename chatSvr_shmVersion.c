
/**
  * Note
  * 	1. SA_RESTART: redo the system call which is interupted by the signal
  */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/epoll.h>
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
#define PROCESS_LIMIT 65536
#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024

struct clientData {
	pid_t pid;
	int connfd;
	int pipefd[2];
	struct sockaddr_in addr;
};

static const char *SHM_NAME = "/tmp/my_shm";

int setnonblock(int fd) {
	int old = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, old | O_NONBLOCK);
	return old;
}

void addfd(int epollfd, int fd) {
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblock(fd);
}

void sighandler(int sig) {
	int tmp = errno;
	send(sigPipefd[1], (char*)&sig, 1, 0);
	errno = tmp;
}

void addsig(int sig, void (*handler)(int), int restart) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if( restart ) {
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig, &sa, NULL) != -1);
}

void delResource() {
	close(sigPipedfd[0]);
	close(sigPipefd[1]);
	close(listenfd);
	close(epollfd);
	shm_unlink(SHM_NAME);
	delete[] users;
	delete[] subProcess;
}

void childTermHandler(int sig) {
	stopChild = 1;
}

int runChild(int idx, clientData *users, char *shareMem) {
	int childEpollfd = epoll_create(5);
	assert(childEpollfd != -1);

	int connfd = users[idx].connfd;
	addfd(childEpollfd, connfd);

	int pipefd = users[idx].pipefd[1];
	addfd(childEpollfd, pipefd);

	int ret;
	addsig(SIGTERM, childTermHandler, false);
	epoll_event events[MAX_EVENT_NUMBER];
	while( !stopChild ) {
		int cnt = epoll_wait(childEpollfd, events, MAX_EVENT_NUMBER, -1);
		if( (cnt < 0) && (errno != EINTR) ) {
			printf("epoll failure\n");
			break;
		}

		for(int i = 0; i < cnt; ++i) {
			int fd = events[i].data.fd;
			if( (fd == connfd) && (events[i].events & EPOLLIN) ) {
				memset(shareMem + idx * BUFFER_SIZE, 0, BUFFER_SIZE);
				if( (ret = recv(connfd, shareMem + idx * BUFFER_SIZE, BUFFER_SIZE - 1, 0)) < 0 ) {
					if( errno != EAGAIN ) {
						stopChild = 1;
					}
				}
				else if( ret == 0 ) {
					stopChild = 1;
				}
				else {
					send(pipefd, (char*)&idx, sizeof(idx), 0);
				}
			}
			else if( (fd == pipefd) && (events[i].events & EPOLLIN) ) {
				int client = 0;
				if( (ret = recv(fd, (char*)&client, sizeof(client), 0)) < 0 ) {
					if( errno != EAGAIN ) {
						stopChild = 1;
					}
				}
				else if( ret == 0 ) {
					stopChild = 1;
				}
				else {
					send(connfd, shareMem + client * BUFFER_SIZE, BUFFER_SIZE, 0);
				}
			}
		}
	}

	close(connfd);
	close(pipefd);
	close(childEpollfd);
	return 0;
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

	struct epoll_event events[MAX_EVENT_NUMBER];
	epollfd = epoll_create(5);
	assert(epollfd != -1);
	addfd(epollfd, listenfd);

	socketpair(AF_UNIX, SOCK_STREAM, 0, sigPipefd);
	setnonblock(sigPipefd[1]);
	addfd(epollfd, sigPipefd[0]);

	userCnt = 0;
	users = new clientData[USER_LIMIT + 1];
	subProcess = new int [PROCESS_LIMIT];
	for(int i = 0; i < PROCESS_LIMIT; ++i) {
		subProcess[i] = -1;
	}

	shmfd = shm_open(SHM_NAME, O_CREATE | O_RDWR, 0666);
	assert(shmfd != -1);
	assert(ftruncate(shmfd, USER_LIMIT * BUFFER_SIZE) != -1);
	shareMem = (char*)mmap(NULL, USER_LIMIT * BUFFER_SIZE, PROT_READ | PORT_WRITE, MAP_SHARE,
			shmfd, 0);
	assert(shareMem != MAP_FAILED);
	close(shmfd);

	addsig(SIGCHLD, sighandler);
	addsig(SIGTERM, sighandler);
	addsig(SIGINT, sighandler);
	addsig(SIGPIPE, SIG_IGN);

	int isStop = 0, isTerminate = 0, userCnt = 0;
	while( !isStop ) {
		int cnt = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
		if( cnt < 0 && errno != EINTR ) {
			printf("epoll failure\n");
			break;
		}

		for(i = 0; i < cnt; ++i) {
			int fd = events[i].data.fd;
			if( fd == listenfd ) {
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

				users[userCnt].addr = clientAddr;
				users[userCnt].connfd = connfd;

				assert(sockpair(AF_UNIX, SOCK_STREAM, 0, users[userCnt].pipefd) != -1);
				pid_t pid = fork();
				if( pid < 0 ) {
					close(connfd);
					printf("create subprocess failure\n");
					continue;
				}
				else if( pid == 0 ) {
					close(epollfd);
					close(listenfd);
					close(users[userCnt].pipefd[0]);
					close(sigPipefd[0]);
					close(sigPipefd[10]);

					runChild(userCnt, users, shareMem);
					munmap((void*)shareMem, USER_LIMIT * BUFFER_SIZE);
					exit(0);
				}
				else {
					close(connfd);
					close(users[userCnt].pipefd[1]);
					addfd(epollfd, users[userCnt].pipefd[0]);
					users[userCnt].pid = pid;

					subProcess[pid] = userCnt++;
				}
			}
			else if( (fd == sigPipefd[0]) && (events[i].events & EPOLLIN) ) {
				int sig;
				char signals[1024];
				if( (ret = recv(sigPipefd[0], signals, sizeof(signals), 0)) == -1 ) {
					printf("recv err[%d]\n", errno);
					continue;
				}
				else if( ret > 0 ) {
					int stat;
					pid_t pid;
					for(int i = 0; i < ret; ++i) {
						switch(signals[i]) {
							case SIGCHLD:
								while( (pid = waitpid(-1, &stat, WNOHANG)) > 0 ) {
									int userIdx = subProcess[pid];
									subProcess[pid] = -1;
									if( userIdx < 0 || userIdx > USER_LIMIT ) {
										continue;
									}

									epoll_ctl(epollfd, EPOLL_CTL_DEL, users[userIdx].pipefd[0], 0);
									close(users[userIdx].pipefd[0]);
									printf("subProcess[users[userIdx].pid] %d , userIdx %d\n",
											subProcess[users[userIdx].pid], userIdx);
									subProcess[users[userIdx].pid] = userIdx;
								}
								if( isTerminate && userCnt == 0 ) {
									isStop = 1;
								}
								break;
							case SIGTERM:
							case SIGINT:
								printf("kill all the child now\n");
								if( userCnt == 0 ) {
									isStop = 1;
									break;
								}

								for(int i = 0; i < userCnt; ++i) {
									kill(users[i].pid, SIGTERM);
								}
								isTerminate = 1;
								break;
							default:
								break;
						}
					}
				}
			}
			else if( events[i].events & EPOLLIN ) {
				int child = 0;
				if( (ret = recv(fd, (char*)&child, sizeof(child), 0)) > 0 ) {
					printf("recv data from child[%d]\n", child);
					for(int j = 0; j < userCnt; ++j) {
						if( users[userCnt].pipefd[0] != fd ) {
							send(users[j].pipefd[0], (char*)&child, sizeof(child), 0);
						}
					}
				}
				else {
					printf("recv erronr[%d]\n", errno);
				}
			}
		}
	}

	delResource();
	return 0;
}

