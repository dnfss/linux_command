
/*
 * Note
 *	   1. This is an echo server which process tcp and udp
 *     2. do IPC through pipe(create by sockpair)
 *     3. demonstrate how to deal with signal with Unified Event Model
 */

#pragma once

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

static int sigPipefd[2];

struct CProcess {
	public:
		CProcess():m_pid(-1) {}

		pid_t m_pid;
		int m_pipefd[2];
};

template<typename T>
class CProcessPool {
	public:
		static CProcessPool<T>* GetInstance(int listenfd, int processNum = 10) {
			if( !m_instance ) {
				m_instance = new CProcessPool<T>(listenfd, processNum);
			}
			return m_instance;
		}

		~CProcessPool() {
			delete [] m_subProcess;
		}

		void Run() {
			if( m_idx != -1 ) {
				RunChild();
				return;
			}
			RunParent();
		}

	private:
		CProcessPool(int listenfd, int processNum):
			m_listenfd(listenfd), m_processNumber(processNum), m_idx(-1), m_stop(false) {
				m_subProcess = new CProcess[m_processNumber];

				for(int i = 0; i < m_processNumber; ++i) {
					int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, m_subProcess[i].m_pipefd);
					if( ret != 0 ) {
						printf("<CProcessPool> create sockpair fail\n");
						continue;
					}

					m_subProcess[i].m_pid = fork();
					if( m_subProcess[i].m_pid > 0 ) {
						close(m_subProcess[i].m_pipefd[1]);
						continue;
					}
					else {
						close(m_subProcess[i].m_pipefd[0]);
						m_idx = i;
						break;
					}
				}
		}

		int SetupSigPipe() {
			m_epollfd = epoll_create(5);
			if( m_epollfd < 0 ) {
				printf("<SetupSigPipe> ERR! epoll_create fail\n");
				return -1;
			}

			int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sigPipefd);
			if( ret < 0 ) {
				printf("<SetupSigPipe> ERR! socketpair fail\n");
				return -2;
			}

			Setnonblock(sigPipefd[1]);
			AddFdToEpoll(m_epollfd, sigPipefd[0]);

			AddSig(SIGCHLD, SigHandler);
			AddSig(SIGTERM, SigHandler);
			AddSig(SIGINT, SigHandler);
			AddSig(SIGPIPE, SigHandler);
			return 0;
		}

		void RunChild() {
			if( SetupSigPipe() < 0 ) {
				return;
			}

			int pipefd = m_subProcess[m_idx].m_pipefd[1];
			AddFdToEpoll(m_epollfd, pipefd);

			struct epoll_event events[MAX_EVENT_NUMBER];
			T *users = new T[USER_PER_PROCESS];

			int ret;
			while( !m_stop ) {
				int num = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
				if( (num < 0 ) && (errno != EINTR) ) {
					printf("<RunChild> epoll_wait fail\n");
					break;
				}

				for(int i = 0; i < num; ++i) {
					int sockfd = events[i].data.fd;
					if( (sockfd == pipefd) && (events[i].events & EPOLLIN) ) {
						int cli;
						if( ((ret = recv(sockfd, (char*)&cli, sizeof(cli), 0)) < 0 && errno != EAGAIN) 
								|| ret == 0 ) {
							continue;
						}
						else {
							struct sockaddr_in cliAddr;
							socklen_t cliAddrLen = sizeof(cliAddr);
							int connfd = accept(m_listenfd, (struct sockaddr*)&cliAddr, &cliAddrLen);
							if( connfd < 0 ) {
								printf("<RunChild> accept fail! errno[%d]\n", errno);
								continue;
							}
							AddFdToEpoll(m_epollfd, connfd);

							// class T must implement function Init
							users[connfd].Init(m_epollfd, connfd, cliAddr);
						}
					}
					else if( (sockfd == sigPipefd[0]) && (events[i].events & EPOLLIN) ) {
						int sig;
						char signals[1024] = {0};
						if( (ret = recv(sockfd, signals, sizeof(signals), 0)) <= 0 ) {
							continue;
						}
						else {
							for(int i = 0; i < ret; ++i) {
								switch(signals[i]) {
									case SIGCHLD:
										pid_t pid;
										int stat;
										while( (pid = waitpid(-1, &stat, WNOHANG)) > 0 ) {
											continue;
										}
										break;
									case SIGTERM:
									case SIGINT:
										m_stop = true;
										break;
									default:
										break;
								}
							}
						}
					}
					else if( events[i].events & EPOLLIN ) {
						users[sockfd].Process();
					}
					else {
						continue;
					}
				}
			}

			delete[] users;
			users = NULL;
			close(pipefd);
			close(m_epollfd);
		}

		void RunParent() {
			if( SetupSigPipe() ) {
				return;
			}
			AddFdToEpoll(m_epollfd, m_listenfd);

			int conCnt = 1, subProcessCnt = 0;
			struct epoll_event events[MAX_EVENT_NUMBER];
			while( !m_stop ) {
				int num = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
				if( (num < 0) && (errno != EINTR ) ) {
					printf("<RunParent> epoll_wait fail\n");
					break;
				}

				for(int i = 0; i < num; ++i) {
					int sockfd = events[i].data.fd;
					if( sockfd == m_listenfd ) {
						int tmp = subProcessCnt;
						do {
							if( m_subProcess[tmp].m_pid != -1 ) {
								break;
							}
							tmp = (tmp + 1) % m_processNumber;
						}while( tmp != subProcessCnt );

						if( m_subProcess[tmp].m_pid == -1 ) {
							m_stop = true;
							break;
						}

						subProcessCnt = (tmp + 1) % m_processNumber;

						send(m_subProcess[tmp].m_pipefd[0], (char*)&conCnt, sizeof(conCnt), 0);
						printf("send request to child %d\n", tmp);
					}
					else if( sockfd == sigPipefd[0] && (events[i].events & EPOLLIN ) ) {
						int sig;
						char signals[1024] = { 0 };
						if( (ret = recv(sockfd, signals, sizeof(signals), 0)) <= 0 ) {
							continue;
						}
						else {
							for(int j = 0; j < ret; ++j) {
								switch( signals[i] ) {
									case SIGCHLD:
										pid_t pid;
										int stat;
										while( (pid = waitpid(-1, &stat, WNOHANG)) > 0 ) {
											for(int k = 0; k < m_processNumber; ++k ) {
												if( m_subProcess[k].m_pid == pid ) {
													printf("<RunParent> child %d exit\n", k);
													close(m_subProcess[k].m_pipefd[0]);
													m_subProcess[k].m_pid = -1;
												}
											}
										}

										m_stop = true;
										for(int k = 0; k < m_processNumber; ++k) {
											if( m_subProcess[k].m_pid != -1 ) {
												m_stop = false;
												break;
											}
										}
										break;
									case SIGTERM:
									case SIGINT:
										printf("<RunParent> kill all the child\n");
										for(int k = 0; k < m_processNumber; ++k) {
											if( m_subProcess[k].m_pid != -1 ) {
												kill(m_subProcess[k].m_pid, SIGTERM);
											}
										}
										break;
									default:
										break;
								}
							}
						}
					}
					else {
						continue;
					}
				}
			}

			close(m_epollfd);
		}

		int m_processNumber;
		int m_idx;
		int m_epollfd;
		int m_listenfd;
		int m_stop;
		CProcess *m_subProcess;
		
		static CProcessPool<T> *m_instance;

		static const int MAX_PROCESS_NUMBER = 32;
		static const int USER_PER_PROCESS = 65536;
		static const int MAX_EVENT_NUMBER = 10000;
};

template<typename T>
CProcessPool<T>* CProcessPool<T>::m_instance = NULL;


static int Setnonblock(int fd) {
	int old = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, old | O_NONBLOCK);
	return old;
}

static void AddFdToEpoll(int epollfd, int fd) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	setnonblock(fd);
}

static void RemoveFdToEpoll(int epollfd, int fd) {
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
}

static void SigHandler(int sig) {
	int save_errno = errno;
	int msg = sig;

	// notify the main loop
	send(pipefd[1], (char*)&msg, 1, 0);
	errno = save_errno;
}

static void AddSig(int sig, void (*handler)(int), bool restart = true) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	if( restart ) {
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);

	int ret;
	if( (ret = sigaction(sig, &sa, NULL)) < 0 ) {
		printf("<addsig> ERR! sigaction ret[%d]\n", ret);
		exit(-1);
	}
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
