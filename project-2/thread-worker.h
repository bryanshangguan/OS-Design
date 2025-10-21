// File:	worker_t.h

// List all group member's name: Kelvin Ihezue,...
// username of iLab: ki120
// iLab Server:
#ifndef WORKER_T_H
#define WORKER_T_H

// thread-worker.h — put BEFORE all #includes
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif


#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_WORKERS macro */
// #define USE_WORKERS 1

/* Targeted latency in milliseconds */
#define TARGET_LATENCY   20  

/* Minimum scheduling granularity in milliseconds */
#define MIN_SCHED_GRN    1

/* Time slice quantum in milliseconds */
#define QUANTUM 10

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
// Included header files 
#include <ucontext.h> 
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>


typedef unsigned int worker_t;

// ADDED CODE 
typedef enum {
	T_READY = 0,
	T_RUNNING,
	T_BLOCKED,
	T_COMPLETED
}thread_status_t;


typedef struct TCB {
	/* add important states in a thread control block */
	// thread Id
	// thread status
	// thread context
	// thread stack
	// thread priority
	// And more ...

	void *(*start_routine)(void *);
  	void *start_arg;

	// YOUR CODE HERE
	worker_t 	t_id;
	ucontext_t  context;
	void 		*stack;
	size_t 		stack_size;

	thread_status_t status; 
	void 		*return_value;
	bool 		is_finished;

	// joining relationships
	worker_t waiting_on;   // tid this thread is currently joining (0 if none)
	worker_t joiner_tid;	// tid of the thread joining me (0 if none)

	// --- separate link pointers for different queues ---
	struct TCB  *rq_next;      // for ready runqueue
	struct TCB *register_next; // for global registry of all threads
	struct TCB *mutex_next;      // for mutex wait queues (later)

}tcb;

// --- PART 1.1 --- 

/* mutex struct definition */ 
typedef struct worker_mutex_t {
	/* add something here */
	int init; 			// 0 = not initialized, 1 = initialized
	worker_t owner;		// tid of the thread that holds the lock (0 = unlocked)

	// YOUR CODE HERE
	tcb *queue_head; // Waiting queue head
	tcb *queue_tail; // Waiting queue tail

} worker_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE
// --- PART 1.1 --- 
typedef struct {
	size_t length;

	tcb *head, *tail;
} ready_queue_t;

/* Function Declarations: */

/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level worker threads voluntarily */
void worker_yield(void); // <-- 1.2 API: declaration 

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);


/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif
