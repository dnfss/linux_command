
/**
  * Note
  * 	1. please man cmsg
  * 	2. http://blog.csdn.net/sparkliang/article/details/5486069
  */

#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static const int CONTROL_LEN = CMSG_LEN(sizeof(int));

void SendFd(int fd, int fdToSend) {
	struct iovec iov[1];
	struct msghdr msg;

	char buf[0];
	iov[0].iov_base = buf;
	iov[0].iov_len = 1;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	struct cmsghdr cm;
	cm.cmsg_len = CONTROL_LEN;
	cm.cmsg_level = SOL_SOCKET;
	cm.cmsg_type = SCM_RIGHTS;
	*(int*)CMSG_DATA(&cm) = fdToSend;

	msg.msg_control = &cm;
	msg.msg_controllen = CONTROL_LEN;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;

	sendmsg(fd, &msg, 0);
}

int RecvFd(int fd) {
	struct iovec iov[1];
	struct msghdr msg;
	char buf[0];
	
	iov[0].iov_base = buf;
	iov[0].iov_len = 1;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	struct cmsghdr cm;
	msg.msg_control = &cm;
	msg.msg_controllen = CONTROL_LEN;

	recvmsg(fd, &msg, 0);
	return *(int*)CMSG_DATA(&cm);
}

int main(int argc, char *argv[] ) {
	int pipefd[2];
	assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, pipefd) != -1);

	pid_t pid = fork();
	assert(pid >= 0);
	if( pid == 0 ) {
		close(pipefd[0]);
		int fd = open("test.txt", O_RDWR, 0666);
		SendFd(pipefd[1], (fd > 0) ? fd : 0);
		close(fd);
		exit(0);
	}
	
	close(pipefd[1]);
	int fd = RecvFd(pipefd[0]);
	
	char buf[1024] = {0};
	read(fd, buf, 1024);
	printf("got fd[%d] and data[%s]\n", fd, buf);
	close(fd);
	return 0;
}
