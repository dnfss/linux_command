
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "utmpx.h"

#define SHOWHOST

void show_info(struct utmpx *record) {
	printf("%-8.8s ", record->ut_user);
	printf("%-8.8s ", record->ut_line);
}

int main(int argc, char *argv[]) {
	int utmpfd;
	struct utmpx current_record;
	int reclen = sizeof(current_record);

	if( (utmpfd = open(UTMPX_FILE, O_RDONLY)) == -1 ) {
		perror(UTMPX_FILE);
		exit(-1);
	}

	while( read(utmpfd, &current_record, reclen) == reclen ) {
		show_info(&current_record);
	}

	close(utmpfd);
	return 0;
}
