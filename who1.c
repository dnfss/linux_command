
// learns how to covert seconds to human readable format. (with function 'ctime')
//
#include "stdio.h"
#include "stdlib.h"
#include "unistd.h"
#include "fcntl.h"
#include "utmp.h"
#include "time.h"

#define SHOWHOST

void show_info(struct utmp *record) {
	if( record->ut_type != USER_PROCESS ) {
		return;
	}

	printf("%-15.15s ", record->ut_user);
	printf("%-15.15s ", record->ut_line);

	time_t t = (time_t)(record->ut_tv.tv_sec);
	printf("%20.20s ", ctime(&t) + 4);

#ifdef SHOWHOST
	printf("(%s)", record->ut_host);
#endif
	printf("\n");
}

int main(int argc, char *argv[]) {
	int utmpfd;
	struct utmp current_record;
	if( (utmpfd = open(UTMP_FILE, O_RDONLY)) == -1 ) {
		perror(UTMP_FILE);
		exit(-1);
	}

	int reclen = sizeof(current_record);
	while( read(utmpfd, &current_record, reclen) == reclen ) {
		show_info(&current_record);
	}

	close(utmpfd);
	return 0;
}
