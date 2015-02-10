/**
  * Note:
  * 	1. IPC_PRIVATE means create a new semaphore
  */

#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

// op == -1 <==> P
// op ==  1 <==> V 
void PV(int semId, int op) {
	struct sembuf buf;
	buf.sem_num = 0;
	buf.sem_op = op;
	// 使用SEM_UNDO，在进程退出时不能显式的做P/V操作，而必须由系统做，否则就等于做了两遍！
	// 在多进程通过信号灯同步资源、而单个进程可能运行较长时间的情况下，不建议使用SEM_UNDO，否则运行进程将长时间占用资源，仅当其退出后才会释放！
	buf.sem_flg = SEM_UNDO;
	semop(semId, &buf, 1);
}

int main(int argc, char *argv[]) {
	int semId = semget(IPC_PRIVATE, 1, 0666);

	union semun semUn;
	semUn.val = 1;
	semctl(semId, 0, SETVAL, semUn);

	pid_t id = fork();
	if( id < 0 ) {
		return -1;
	}
	else if( id == 0 ) {
		printf("child try to get semaphore\n");
		PV(semId, -1);
		printf("child get semaphore and would release it after 3s\n");
		sleep(3);
		exit(0);
	}
	else {
		sleep(1);
		printf("parent try to get semapthore\n");
		PV(semId, -1);
		printf("parent get semaphore and would release it after 3s\n");
		sleep(3);
		PV(semId, 1);
	}

	waitpid(id, NULL, 0);
	semctl(semId, 0, IPC_RMID, semUn);
	return 0;
}
