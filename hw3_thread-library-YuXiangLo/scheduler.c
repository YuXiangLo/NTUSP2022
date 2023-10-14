#include "threadtools.h"

/*
 * Print out the signal you received.
 * If SIGALRM is received, reset the alarm here.
 * This function should not return. Instead, call longjmp(sched_buf, 1).
 */
void sighandler(int signo) {
    // TODO
	if(signo == SIGALRM){
		puts("caught SIGALRM");
		alarm(timeslice);
 		siglongjmp(sched_buf, YIELD);
	} else if(signo == SIGTSTP){
		puts("caught SIGTSTP");
		siglongjmp(sched_buf, YIELD);
	}
}

/*
 * Prior to calling this function, both SIGTSTP and SIGALRM should be blocked.
 */

void scheduler() {
    // TODO
	int where = sigsetjmp(sched_buf, 69);
	struct timeval t = {0, 0};
	fd_set wq_set;
	FD_ZERO(&wq_set);
	for(int i = 0; i < wq_size; i++)
		FD_SET(waiting_queue[i]->fd, &wq_set);
	int selectRet = select(1024, &wq_set, NULL, NULL, &t);
	if(selectRet > 0)
		for(int i = 0; i < wq_size; i++)
			if(FD_ISSET(waiting_queue[i]->fd, &wq_set)){
				ready_queue[rq_size++] = waiting_queue[i];
				for(int j = i; j < wq_size - 1; j++)
					waiting_queue[j] = waiting_queue[j + 1];
				waiting_queue[--wq_size] = NULL;
				--i;
			}

	switch(where){
		case YIELD:
			if(++rq_current == rq_size) rq_current = 0;
			break;
		case AREAD:
			waiting_queue[wq_size++] = ready_queue[rq_current];
			ready_queue[rq_current] = ready_queue[--rq_size];
			ready_queue[rq_size] = NULL;
			if(rq_current == rq_size) rq_current = 0;
			break;
		case EXIT:
			if(rq_current == rq_size - 1){
				rq_current = 0, rq_size--;
				ready_queue[rq_size] = NULL;
			} else {
				ready_queue[rq_current] = ready_queue[--rq_size];
				ready_queue[rq_size] = NULL;
			}
			break;
	}
	if(!rq_size){
		if(!wq_size) return;
		FD_ZERO(&wq_set);
		for(int i = 0; i < wq_size; i++)
			FD_SET(waiting_queue[i]->fd, &wq_set);
		int selectRet = select(getdtablesize(), &wq_set, NULL, NULL, NULL);
		if(selectRet > 0)
			for(int i = 0; i < wq_size; i++){
				if(FD_ISSET(waiting_queue[i]->fd, &wq_set)){
					ready_queue[rq_size++] = waiting_queue[i];
					for(int j = i; j < wq_size - 1; j++)
						waiting_queue[j] = waiting_queue[j + 1];
					waiting_queue[--wq_size] = NULL;
					--i;
				}
			}
	}
	siglongjmp(RUNNING->environment, 69);
}
