#ifndef THREADTOOL
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#define THREADTOOL

#define THREAD_MAX 16  // maximum number of threads created
#define BUF_SIZE 512

#define YIELD 1
#define AREAD 2
#define EXIT 3

typedef struct tcb {
    int id;  // the thread id
    jmp_buf environment;  // where the scheduler should jump to
    int arg;  // argument to the function
    int fd;  // file descriptor for the thread
	char fifoName[128];
    char buf[BUF_SIZE];  // buffer for the thread
    int i, x, y;  // declare the variables you wish to keep between switches
} tcb;

extern int timeslice;
extern jmp_buf sched_buf;
extern struct tcb *ready_queue[THREAD_MAX], *waiting_queue[THREAD_MAX];
/*
 * rq_size: size of the ready queue
 * rq_current: current thread in the ready queue
 * wq_size: size of the waiting queue
 */
extern int rq_size, rq_current, wq_size;
/*
* base_mask: blocks both SIGTSTP and SIGALRM
* tstp_mask: blocks only SIGTSTP
* alrm_mask: blocks only SIGALRM
*/
extern sigset_t base_mask, tstp_mask, alrm_mask;
/*
 * Use this to access the running thread.
 */
#define RUNNING (ready_queue[rq_current])

void sighandler(int signo);
void scheduler();

#define thread_create(func, id, arg) {\
	func(id, arg);\
}
#define thread_setup(id, arg) {\
	ready_queue[rq_size] = malloc(sizeof(tcb));\
	ready_queue[rq_size]->id = id;\
	ready_queue[rq_size]->x = ready_queue[rq_size]->y = 0;\
	ready_queue[rq_size]->arg = arg;\
	sprintf(ready_queue[rq_size]->fifoName, "%d_%s", id, __func__);\
	mkfifo(ready_queue[rq_size]->fifoName, 0644);\
	ready_queue[rq_size]->fd = \
		open(ready_queue[rq_size]->fifoName, O_RDWR | O_NONBLOCK);\
	if(!sigsetjmp(ready_queue[rq_size++]->environment, 69))\
		return;\
}

#define thread_exit() {\
	remove(RUNNING->fifoName);\
	close(RUNNING->fd);\
	free(RUNNING);\
	siglongjmp(sched_buf, EXIT);\
}

#define thread_yield() {\
	if(!sigsetjmp(RUNNING->environment, 69)){\
		sigprocmask(SIG_SETMASK, &alrm_mask, NULL);\
		sigprocmask(SIG_SETMASK, &tstp_mask, NULL);\
		sigprocmask(SIG_SETMASK, &base_mask, NULL);\
	}\
}

#define async_read(count) {\
	if(!sigsetjmp(RUNNING->environment, 69))\
		siglongjmp(sched_buf, AREAD);\
	else\
		read(RUNNING->fd, RUNNING->buf, sizeof(char) * count);\
}

#endif // THREADTOOL
