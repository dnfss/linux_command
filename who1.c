
/*
 * Note
 *	 covert seconds to human readable format. (with function 'ctime')
 *	 reduce system call with carton
 *	 This implementation is not thread safe
 */

#include <utmp.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SHOWHOST
#define MAX_RECORDS 16

static int curIdx = 0;
static int utmp_fd = -1;
static int recordCnt = 0;
static char utmpbuf[MAX_RECORDS * sizeof(struct utmp)] = {0};

int utmp_open(const char *filename) {
	utmp_fd = open(filename, O_RDONLY);
	curIdx = 0;
	recordCnt = 0;
	return utmp_fd;
}

// This function is not thread safe
struct utmp* utmp_next() {
	if( utmp_fd == -1 ) {
		return NULL;
	}

	if( curIdx == recordCnt ) {	// reload
		int readSize = read(utmp_fd, utmpbuf, MAX_RECORDS * sizeof(struct utmp));
		if( (recordCnt = readSize / sizeof(struct utmp)) == 0 ) {
			return NULL;
		}

		curIdx = 0;
	}

	return (struct utmp*)(&utmpbuf[curIdx++ * sizeof(struct utmp)]);
}

void utmp_close() {
	if( utmp_fd != -1 ) {
		close(utmp_fd);
		utmp_fd = -1;
	}
}

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
	if( utmp_open(UTMP_FILE) == -1 ) {
		perror(UTMP_FILE);
		exit(-1);
	}

	struct utmp *current_record;
	while( (current_record = utmp_next()) != NULL ) {
		show_info(current_record);
	}

	utmp_close();
	return 0;
}
