
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>

const int HOSTLEN= 256;

int connect_to_server(char *host, int port) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if( sock == -1 ) {
		return -1;
	}

	struct sockaddr_in svradd;
	bzero(&svradd, sizeof(svradd));
	svradd.sin_family = AF_INET;
	svradd.sin_port = htons(port);

	struct hostent *hp = gethostbyname(host);
	if( hp == NULL ) {
		return -2;
	}
	bcopy(hp->h_addr, (struct sockaddr *)&svradd.sin_addr, hp->h_length);

	if( connect(sock, (struct sockaddr *)&svradd, sizeof(svradd)) != 0 ) {
		return -3;
	}

	return sock;
}

void talk_with_server(int fd) {
	char buf[256];
	int n = read(fd, buf, sizeof(buf));

	write(1, buf, n);
}

int main(int argc, char *argv[]) {
	char host[HOSTLEN];
	gethostname(host, HOSTLEN);
	
	int fd = connect_to_server(host, 12345);
	if( fd < 0 ) {
		printf("ERR: onnect_to_server ret[%d], failed to connect [%s:%d]\n",
				fd, host, 12345);
		return -1;
	}

	talk_with_server(fd);
	close(fd);
	return 0;
}
		
