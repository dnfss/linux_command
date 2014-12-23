
/*
 * Note:
 *	1. fclose will close the fd, too !!!
 *
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
#include <sys/stat.h>

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

void cannot_do(int fd) {
	FILE *fp = fdopen(fd, "w");

	fprintf(fp, "HTTP/1.0 501 Not Implemented\r\n");
	fprintf(fp, "Content-type: test/plain\r\n");
	fprintf(fp, "\r\n");

	fprintf(fp, "That command is not yet implemented\r\n");
	fclose(fp);
}

int not_exist(char *f) {
	struct stat info;
	return stat(f, &info) == -1;
}

void do_404(int fd, char *item) {
	FILE *fp = fdopen(fd, "w");

	fprintf(fp, "HTTP/1.0 404 Not Found\r\n");
	fprintf(fp, "Content-type: text/plain\r\n");
	fprintf(fp, "\r\n");

	fprintf(fp, "The item requested: %s\r\n is not found\r\n", item);
	fclose(fp);
}

int isdir(char *f) {
	struct stat info;
	return stat(f, &info) != -1
		&& S_ISDIR(info.st_mode);
}

void do_ls(int fd, char *dir) {
	FILE *fp = fdopen(fd, "w");
	fprintf(fp, "HTTP/1.0 200 OK\r\n");
	fprintf(fp, "content-type: text/plain\r\n");
	fprintf(fp, "\r\n");
	fflush(fp);

	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);

	execlp("ls", "ls", "-l", dir, NULL);
}

char* file_type(char *f) {
	char *pc;
	if( (pc = strchr(f, '.')) != NULL ) {
		return pc + 1;
	}
	return "";
}

void do_exec(int fd, char *prog) {
	FILE *fp = fdopen(fd, "w");
	fprintf(fp, "HTTP/1.0 200 0K\r\n");
	fflush(fp);

	dup2(fd, 1);
	dup2(fd, 2);
	close(fd);

	execl(prog, prog, NULL);
}

void do_cat(int fd, char *f) {
	char *extension = file_type(f);
	char *content = "text/plain";

	if( strcmp(extension, "html") == 0 ) {
		content = "text/html";
	}
	else if( strcmp(extension, "gif") == 0 ) {
		content = "image/gif";
	}
	else if( strcmp(extension, "jpg") == 0 ) {
		content = "image/jpeg";
	}
	else if( strcmp(extension, "jpeg") == 0 ) {
		content = "image/jpeg";
	}

	FILE *fpsock = fdopen(fd, "w");
	if( fpsock == NULL ) {
		printf("ERR: can not write to socket\n");
		return;
	}
	fprintf(fpsock, "HTTP/1.0 200 OK\r\n");
	fprintf(fpsock, "Content-type: %s\r\n", content);
	fprintf(fpsock, "\r\n");

	FILE *fpfile = fopen(f, "r");
	if( fpfile == NULL ) {
		printf("ERR: can not open %s\n", f);
		return;
	}

	char c;
	while( (c = getc(fpfile)) != EOF ) {
		putc(c, fpsock);
	}

	fclose(fpsock);
	fclose(fpfile);
	printf("do_cat SUC! filename[%s]\n", f);
}

void dispatch(int fd, char *request) {
	char cmd[256], arg[256];
	strcpy(arg, "./");

	if( sscanf(request, "%s %s", cmd, arg + 2) != 2 ) {
		return;
	}

	if( strcmp(cmd, "GET") != 0 ) {
		printf("in cannot_do\n");
		cannot_do(fd);
	}
	else if( not_exist(arg) ) {
		printf("in do_404\n");
		do_404(fd, arg);
	}
	else if( isdir(arg) ) {
		printf("in do_ls\n");
		do_ls(fd, arg);
	}
	else if( strcmp(file_type(arg + 2), "cgi") == 0 ) {
		printf("in do_exec\n");
		do_exec(fd, arg);
	}
	else {
		printf("in do_cat\n");
		do_cat(fd, arg);
	}
}

// support the GET command ONLY
void process_request(int fd) {
	FILE *fpin = fdopen(fd, "r");

	// read request
	char request[256] ={0};
	fgets(request, sizeof(request), fpin);
	printf("got request[%s]\n", request);

	// skip over all request info
	char buf[256];
	while( fgets(buf, sizeof(buf), fpin) != NULL
			&& strcmp(buf, "\r\n") != 0 ) ;
	dispatch(fd, request);

	// attention: fclose will close the fd, too !!!
	fclose(fpin);
}

void child_waiter(int signum) {
	while( waitpid(-1, NULL, WNOHANG) > 0 );
}

int main(int argc, char *argv[]) {
	if( argc == 1 ) {
		fprintf(stderr, "usage websvr port_num\n");
		return -1;
	}

	int sock = make_server_socket(atoi(argv[1]));
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

