
/**
  * Note
  * 	continue while accept() return -1 if errno equals EINTR
  * 	use waitpid() to deal with multiple SIGCHLD
  */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <strings.h>
#include <sys/wait.h>
#include <errno.h>

const int HOST_LEN = 256;
const int BACKLOG = 256;

int make_server_socket(int port, int backlog = BACKLOG) {
	int socket_id = socket(AF_INET, SOCK_STREAM, 0);
	if( socket_id == -1 ) {
		return -1;
	}

	struct sockaddr_in addr;
	bzero((void*)&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	char hostname[HOST_LEN];
	gethostname(hostname, HOST_LEN);
	struct hostent *hp = gethostbyname(hostname);
	bcopy((void*)hp->h_addr, (void*)&addr.sin_addr, hp->h_length);

	if( bind(socket_id, (struct sockaddr *)&addr, sizeof(addr)) != 0 ) {
		return -1;
	}

	if( listen(socket_id, backlog) != 0 ) {
		return -1;
	}

	return socket_id;
}

void process_request(int fd) {
	dup2(fd, 1);
	close(fd);
	execl("/bin/date", "date", NULL);
}

void child_waiter(int signum) {
	while( waitpid(-1, NULL, WNOHANG) > 0 );
}

int main(int argc, char *argv[]) {
	int sock = make_server_socket(12345);
	if( sock == -1 ) {
		return -1;
	}

	signal(SIGCHLD, child_waiter);
	while( true ) {
		int fd = accept(sock, NULL, NULL);
		if( fd == -1 && errno == EINTR ) {
			continue;
		}

		if( fd == -1 ) {
			break;
		}

		int pid = fork();
		if( pid == 0 ) {	// child
			process_request(fd);
			exit(0);
		}
		else if( pid > 0 ) {
			close(fd);
		}
	}

	return 0;
}

