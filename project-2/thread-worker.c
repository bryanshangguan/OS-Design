// File:  thread-worker.c
// List all group member's name: Kelvin Ihezue, Bryan Shangguan
// username of iLab: ki120, bys8

#include "thread-worker.h"
#include <errno.h>
#include <stdlib.h>

#define MAIN_OWNER ((worker_t)~0u)

/* =========================================================================
 * Library global state
 * ========================================================================= */

#define STACK_BYTES SIGSTKSZ

//Global counter for total context switches and 
//average turn around and response time
long tot_cntx_switches=0;
double avg_turn_time=0;
double avg_resp_time=0;
static long completed_threads = 0;

// === Library state ===
static ucontext_t scheduler_context;
static ucontext_t main_context; 
static tcb *curr = NULL;
static tcb *all_threads = NULL;  // singly-linked list of all TCBs (READY/BLOCKED/COMPLETED)
static int library_init = 0;
static worker_t next_tid = 1;

// A simple FIFO ready queue
static ready_queue_t run_queue = {0};

// 1.1.5 Timers: globals + handler 
static struct sigaction sig_a; // signal action for SIGPROF
static struct itimerval t_intval; // interval + current value for ITIMER_PROF

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static void schedule(void);
static void _trampoline(void);
static void t_handler(int signal_num);
static void t_ms_start(int quantum_ms);

/* ready-queue */
static void rq_init(ready_queue_t *q);
static void rq_enqueue(tcb *t);
static tcb* rq_dequeue(void);

/* registry */
static void registry_add(tcb *t);
static tcb* find_tcb(worker_t tid);

/* scheduler algorithm prototypes so schedule() can call them */
static void sched_psjf(void);
static void sched_mlfq(void);
static void sched_cfs(void);

#define NUM_LEVELS 4
static ready_queue_t mlfq_queues[NUM_LEVELS];
static const unsigned long mlfq_time_slice_ms[NUM_LEVELS] = {10, 20, 40, 80};
#define PRIORITY_BOOST_S 2
static struct timeval last_boost_time = {0};

static void mlfq_enqueue(tcb *t);
static tcb* mlfq_dequeue(void);
static void cfs_heap_insert(tcb *t);
static tcb* cfs_heap_pop_min(void);

// for CFS
typedef struct {
    tcb **heap_array; // Dynamic array to store TCB pointers
    int capacity;     // Max number of elements
    int size;         // Current number of elements
} cfs_min_heap_t;

static cfs_min_heap_t cfs_runqueue = {0};
static void cfs_heap_init(cfs_min_heap_t *h, int initial_capacity);
static void cfs_heap_insert(cfs_min_heap_t *h, tcb *t);
static tcb* cfs_heap_peek_min(const cfs_min_heap_t *h);
static tcb* cfs_heap_pop_min(cfs_min_heap_t *h);
static int cfs_heap_size(const cfs_min_heap_t *h);
static void cfs_heap_destroy(cfs_min_heap_t *h);

static inline int cfs_parent(int i) { return (i - 1) / 2; }
static inline int cfs_left(int i) { return 2 * i + 1; }
static inline int cfs_right(int i) { return 2 * i + 2; }

static struct timeval quantum_start_time;

// --- Scheduler-aware Enqueue function ---
static void scheduler_enqueue(tcb *t); // Declaration

/* mutex wait-queue */
static inline void mutex_enqueue(worker_mutex_t *m, tcb *t);
static inline tcb* mutex_dequeue(worker_mutex_t *m);

/* utility for timers */
static inline void convert_ms_itimerval(int ms, struct timeval *t_val);

static inline void cfs_swap(tcb **a, tcb **b) {
    tcb *temp = *a; *a = *b; *b = temp;
}

// sift down helper
static void cfs_heap_sift_down(cfs_min_heap_t *h, int idx) {
    int min_idx = idx;
    int l = cfs_left(idx);
    int r = cfs_right(idx);

    if (l < h->size && h->heap_array[l]->vruntime_us < h->heap_array[min_idx]->vruntime_us) {
        min_idx = l;
    }
    if (r < h->size && h->heap_array[r]->vruntime_us < h->heap_array[min_idx]->vruntime_us) {
        min_idx = r;
    }

    if (idx != min_idx) {
        cfs_swap(&h->heap_array[idx], &h->heap_array[min_idx]);
        cfs_heap_sift_down(h, min_idx);
    }
}

// sift up helper
static void cfs_heap_sift_up(cfs_min_heap_t *h, int idx) {
    while (idx > 0 && h->heap_array[cfs_parent(idx)]->vruntime_us > h->heap_array[idx]->vruntime_us) {
        cfs_swap(&h->heap_array[idx], &h->heap_array[cfs_parent(idx)]);
        idx = cfs_parent(idx);
    }
}

static void cfs_heap_init(cfs_min_heap_t *h, int initial_capacity) {
    h->heap_array = malloc(initial_capacity * sizeof(tcb*));
    if (!h->heap_array) { exit(1); }
    h->capacity = initial_capacity;
    h->size = 0;
}

static void cfs_heap_insert(cfs_min_heap_t *h, tcb *t) {
    if (h->size == h->capacity) {
        fprintf(stderr, "CFS heap full, resizing not implemented.\n");
        return; 
    }

    h->heap_array[h->size] = t;
    h->size++;
    
    cfs_heap_sift_up(h, h->size - 1);
}

static tcb* cfs_heap_peek_min(const cfs_min_heap_t *h) {
    if (h->size == 0) return NULL;
    return h->heap_array[0];
}

static tcb* cfs_heap_pop_min(cfs_min_heap_t *h) {
    if (h->size == 0) return NULL;

    tcb* min_tcb = h->heap_array[0];

    h->heap_array[0] = h->heap_array[h->size - 1];
    h->size--;

    cfs_heap_sift_down(h, 0);

    return min_tcb;
}

static int cfs_heap_size(const cfs_min_heap_t *h) {
    return h->size;
}

static void cfs_heap_destroy(cfs_min_heap_t *h) {
    free(h->heap_array);
    h->size = 0;
    h->capacity = 0;
}

// for MLFQ
static void mlfq_enqueue(tcb *t) {
    if (!t || t->priority < 0 || t->priority >= NUM_LEVELS) return;
    ready_queue_t *q = &mlfq_queues[t->priority];
    t->rq_next = NULL;
    if (!q->tail) q->head = q->tail = t;
    else { q->tail->rq_next = t; q->tail = t; }
    q->length++;
}

static tcb* mlfq_dequeue_level(int level) {
    if (level < 0 || level >= NUM_LEVELS) return NULL;
    ready_queue_t *q = &mlfq_queues[level];
    tcb *t = q->head;
    if (!t) return NULL;
    q->head = t->rq_next;
    if (!q->head) q->tail = NULL;
    t->rq_next = NULL;
    q->length--;
    return t;
}

/* =========================================================================
 * Ready queue helpers
 * ========================================================================= */

// Initialize a ready queue.
static void rq_init(ready_queue_t *q) {     
    q->head = q->tail = NULL;
    q->length = 0;
}

// PART 1.1.4
static void rq_enqueue(tcb *t) {
  t->rq_next = NULL;
  if (!run_queue.tail) run_queue.head = run_queue.tail = t;
  else { run_queue.tail->rq_next = t; run_queue.tail = t; }
  run_queue.length++;
}

// PART 1.1.4
static tcb* rq_dequeue(void) {
  tcb *t = run_queue.head;
  if (!t) return NULL;
  run_queue.head = t->rq_next;
  if (!run_queue.head) run_queue.tail = NULL;
  t->rq_next = NULL;
  run_queue.length--;
  return t;
}

/* =========================================================================
 * Registry helpers (for join/find)
 * ========================================================================= */
static void registry_add(tcb *t) {
    t->register_next = all_threads;
    all_threads = t;
}

static tcb* find_tcb(worker_t tid) {
    for (tcb *p = all_threads; p; p = p->register_next) {
        if (p->t_id == tid) return p;
    }
    return NULL;
}

/* =========================================================================
 * Mutex wait-queue helpers (Part 1.5)
 * ========================================================================= */

// Enqueue a TCB at the tail of a mutex's waiting queue
static inline void mutex_enqueue(worker_mutex_t *m, tcb *t) {
    t->mutex_next = NULL;
    if (!m->queue_tail) { m->queue_head = m->queue_tail = t; }
    else { 
        m->queue_tail->mutex_next = t;
        m->queue_tail = t;
    }
}

// Dequeue the head TCB from a mutex's waiting queue
static inline tcb* mutex_dequeue(worker_mutex_t *m) {
    tcb *t = m->queue_head;
    if (!t) return NULL;
    m->queue_head = t->mutex_next;
    if (!m->queue_head) m->queue_tail = NULL;
    t->mutex_next = NULL;
    return t;
}

/* =========================================================================
 * Timer helpers (Part 1.1.5)
 * ========================================================================= */

// Convert milliseconds to an itimerval {seconds, useconds}
static inline void convert_ms_itimerval(int ms, struct timeval *t_val) {
    if (ms < 0) ms = 0;
    t_val->tv_sec  = ms / 1000;
    t_val->tv_usec = (ms % 1000) * 1000;
}

static void t_ms_start(int quantum_ms) {
    // 1) Install the SIGPROF handler *via sigaction()
    memset(&sig_a, 0, sizeof(sig_a));
    sig_a.sa_handler = t_handler;
    sigemptyset(&sig_a.sa_mask);
    sig_a.sa_flags = SA_RESTART; // auto-restart some syscalls
    if (sigaction(SIGPROF, &sig_a, NULL) != 0) {
        perror("sigaction(SIGPROF)");
        exit(1);
    }

    // Program ITIMER_PROF: periodic firing
    memset(&t_intval, 0, sizeof(t_intval));
    convert_ms_itimerval(quantum_ms, &t_intval.it_value);
    convert_ms_itimerval(quantum_ms, &t_intval.it_interval);

    if (setitimer(ITIMER_PROF, &t_intval, NULL) != 0) {
        perror("setitimer(ITIMER_PROF)");
        exit(1);
    }
}

static void t_handler(int signal_num) {
    (void) signal_num;
    if (!curr) return;

    curr->status = T_PREEMPTED;
    swapcontext(&curr->context, &scheduler_context);
}

/* =========================================================================
 * scheduler
 * ========================================================================= */

//DO NOT MODIFY THIS FUNCTION
/* scheduler */ 
static void schedule() {
	// - every time a timer interrupt occurs, your worker thread library 
	// should be contexted switched from a thread context to this 
	// schedule() function
	
	//YOUR CODE HERE

#if defined(PSJF)
    sched_psjf();
#elif defined(MLFQ)
    sched_mlfq();
#elif defined(CFS)
    sched_cfs();
#else
    # error "Define one of PSJF, MLFQ, or CFS when compiling. e.g. make SCHED=MLFQ"
#endif
}

/* =========================================================================
 * Public API
 * ========================================================================= */

// Part 1: 1.1
/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, void *(*function)(void*), void * arg) {
    // - create Thread Control Block (TCB)
    // - create and initialize the context of this worker thread
    // - allocate space of stack for this thread to run
    // after everything is set, push this thread into run queue and 
    // - make it ready for the execution.

    (void) attr; // Ignored for now

    // YOUR CODE HERE

    // --- ONE_TIME LIBRARY INITIALIZER (inline) ---
    if (!library_init) {
        library_init = 1;

        // Initialize structures for ALL schedulers
        rq_init(&run_queue); // For PSJF
        for (int i = 0; i < NUM_LEVELS; ++i) { // For MLFQ
            rq_init(&mlfq_queues[i]);
        }
        cfs_heap_init(&cfs_runqueue, 100); // For CFS (adjust capacity)
        gettimeofday(&last_boost_time, NULL); // Init boost timer for MLFQ


        // capture main context so the scheduler can return to it
        getcontext(&main_context);

        // build scheduler context + stack
        // ... (rest of the initializer is okay) ...
        makecontext(&scheduler_context, schedule, 0);

        // 1.1.5 Timers: arm the periodic preemption timer
        // NOTE: Timer start might need adjustment depending on scheduler
        // For MLFQ/CFS, the *first* timer interval might be set by the scheduler itself.
        // For simplicity, we start it here, but schedulers will override `it_value`.
        t_ms_start(QUANTUM); // QUANTUM is a default, schedulers will set specifics
    }
    
    // 1) allocate space of stack for this thread to run
    tcb *t = (tcb*)calloc(1, sizeof(*t));
    if (!t) {errno = ENOMEM; return -1;}

    t->t_id = next_tid++;
    t->status = T_READY;
    t->is_finished  = false;
    t->return_value = NULL;
    t->joiner_tid = 0; // Num of threads joined
    t->waiting_on = 0;

    // 2) create and initialize the context of this worker thread
    getcontext(&t->context);
    t->stack_size = STACK_BYTES;
    t->stack = malloc(t->stack_size);
    if (!t->stack) {free(t); errno = ENOMEM; return -1;}

    t->context.uc_stack.ss_sp = t->stack;
    t->context.uc_stack.ss_size = t->stack_size;
    // when thread function returns, jump to scheduler
    t->context.uc_link = &scheduler_context;

    // fix:
    t->start_routine = function;
    t->start_arg     = arg;

    /* Use a 0-arg trampoline so we don't pass pointers via varargs. */
    makecontext(&t->context, (void(*)(void))_trampoline, 0);

    gettimeofday(&t->creation_time, NULL);

    // Register so joins/exits can find it even when not on run queue
    registry_add(t);
    
    // Inline Enqueue to push READY tcb 't' to tail of run_queue
    // 1.1.4 rq_enqueue(t);
    scheduler_enqueue(t);

    if (thread) {
        *thread = t->t_id;
    }
    return 0;
};

// Part 1.2 
/* give CPU possession to other user-level worker threads voluntarily */
void worker_yield(void) {
	
	// - change worker thread's state from Running to Ready
	// - save context of this thread to its thread control block
	// - switch from thread context to scheduler context

	// YOUR CODE HERE

    sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

	if (!curr) {
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        swapcontext(&main_context, &scheduler_context);
        perror("swapcontext failed in worker_yield (main)");
        abort();
    }

	curr->status = T_READY;
	// rq_enqueue(curr);
    scheduler_enqueue(curr);

	tcb *me = curr; // me points to the same TCB as curr
	curr = NULL; 	// no current while the scheduler runs

	sigprocmask(SIG_SETMASK, &oldset, NULL);
    swapcontext(&me->context, &scheduler_context);
    perror("swapcontext failed in worker_yield (worker)");
    abort();
};

/* terminate a thread */
void worker_exit(void *value_ptr) {
	// - de-allocate any dynamic memory created when starting this thread

	// YOUR CODE HERE
	if (!curr) {
		setcontext(&scheduler_context);
		// If setcontext returns for any reason, bail
		abort();
	};

	// Keep a local alias; after we null out 'curr' we still need this TCB.
	tcb *me = curr;

	// 1) Save the return value passed by the thread function
	me->return_value = value_ptr;

    // block signals during metric update
    sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

    gettimeofday(&me->completion_time, NULL);

    unsigned long create_us = me->creation_time.tv_sec * 1000000UL + me->creation_time.tv_usec;
    unsigned long first_run_us = me->first_run_time.tv_sec * 1000000UL + me->first_run_time.tv_usec;
    unsigned long complete_us = me->completion_time.tv_sec * 1000000UL + me->completion_time.tv_usec;

    if (me->has_run_before) {
        unsigned long new_turnaround_time_us = complete_us - create_us;
        unsigned long new_response_time_us = first_run_us - create_us;

        avg_turn_time = (avg_turn_time * completed_threads + (new_turnaround_time_us / 1000.0)) / (completed_threads + 1);
        avg_resp_time = (avg_resp_time * completed_threads + (new_response_time_us / 1000.0)) / (completed_threads + 1);

        completed_threads++;
    }

    // unblock signals
    sigprocmask(SIG_SETMASK, &oldset, NULL);

	// 2) Mark logical completion in the TCB.
	curr->is_finished = true;
	curr->status = T_COMPLETED;

	// Part 1.4: Wake the joiner if one exists
	if (me->joiner_tid) {
        tcb *j = find_tcb(me->joiner_tid);

        if (j && j->waiting_on == me->t_id && j->status == T_BLOCKED) {
            sigset_t oldset, blockset;
            sigemptyset(&blockset);
            sigaddset(&blockset, SIGPROF);
            sigprocmask(SIG_BLOCK, &blockset, &oldset);

            j->waiting_on = 0;
            j->status = T_READY;
            scheduler_enqueue(j);

            sigprocmask(SIG_SETMASK, &oldset, NULL);
        }
    }
	
	// 4) This thread is done running; clear 'curr' so the scheduler owns the CPU.
	curr = NULL;

    setcontext(&scheduler_context);
    perror("setcontext failed in worker_exit");
    abort();
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr) {
	
	// - wait for a specific thread to terminate
	// - de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE

	// 1. Lookup & basic validation (find target)
	tcb *target = find_tcb(thread);
	if (!target) { errno = ESRCH; return -1; } // no such thread
	if (curr && curr->t_id == thread) {errno = EDEADLK; return -1;} // self-join would deadlock

	// 2. If target already finished, harvest result and free resources
	if (target->status == T_COMPLETED) {
		if (value_ptr) *value_ptr = target->return_value;
		free(target->stack);
		return 0;
	}

	if (curr) {
		// 3. Record the join relationship
		target->joiner_tid = curr->t_id;  // target knows who to wake
		curr->waiting_on = thread;  	// caller records who it waits for 
		curr->status = T_BLOCKED;

		// 4. Switch to the scheduler; we’ll resume once target calls worker_exit
		swapcontext(&curr->context, &scheduler_context);
	} else {
		// main thread joins → run scheduler until target completes
		while (target->status != T_COMPLETED) {
			swapcontext(&main_context, &scheduler_context);
		}
	}

	// 5. resumed: target should be COMPLETED now
	target = find_tcb(thread);
	if (target && target->status == T_COMPLETED) {
		if (value_ptr) {
			*value_ptr = target->return_value;
		}
		free(target->stack);
		return 0;
	}

	// If we got here, either target got detached or missing
	errno = EINVAL;
	return -1;
};

// === Part 1.5: Mutex implementation ===

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//- initialize data structures for this mutex

	(void)mutexattr;

	// YOUR CODE HERE
	if (!mutex) {errno = EINVAL; return -1;}

	mutex->owner = 0; 		// unlocked
    mutex->queue_head = NULL;
	mutex->queue_tail = NULL; 
	mutex->init = 1;
	return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex) {

    // - use the built-in test-and-set atomic function to test the mutex
    // - if the mutex is acquired successfully, enter the critical section
    // - if acquiring mutex fails, push current thread into block list and
    // context switch to the scheduler thread

    // YOUR CODE HERE

    sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

    // fast path
    if (mutex->owner == 0) {
        mutex->owner = curr->t_id;
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        return 0;
    }

    // slow path
    curr->status = T_BLOCKED;
    mutex_enqueue(mutex, curr);
    
    tcb *me = curr; 
    curr = NULL;

    sigprocmask(SIG_SETMASK, &oldset, NULL); 
    swapcontext(&me->context, &scheduler_context);

    if (mutex->owner != me->t_id) {
        errno = EPERM; 
        return -1;
    }
    return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	if (!mutex || !mutex->init) {errno = EINVAL; return -1;}
    
    sigset_t oldset, blockset;
    worker_t expected_owner = curr ? curr->t_id : MAIN_OWNER;

    if (mutex->owner != expected_owner) {
        errno = EPERM;
        return -1;
    }

	// block signals before checking/modifying mutex queue and ready queue
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

    tcb *next_owner = mutex_dequeue(mutex);
    if (next_owner) {
        mutex->owner = next_owner->t_id;
        next_owner->status = T_READY;
        scheduler_enqueue(next_owner);
    } else {
        mutex->owner = 0;
    }

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex) {
	// - de-allocate dynamic memory created in worker_mutex_init

	if (!mutex || !mutex->init) {errno = EINVAL; return -1;}
	if (mutex->owner != 0 || mutex->queue_head != NULL) {errno = EBUSY; return -1;}

	// No dynamic memory was allocated inside the struct, so nothing to free.
    // Just mark it deinitialized and clear fields to a known state.

	mutex->init = 0;
	mutex->owner = 0;
	mutex->queue_head = NULL;
	mutex->queue_tail = NULL;
	return 0;
};

static void scheduler_enqueue(tcb *t) {
    if (!t || t->status != T_READY) return;

#if defined(PSJF)
    rq_enqueue(t);
#elif defined(MLFQ)
    mlfq_enqueue(t);
#elif defined(CFS)
    cfs_heap_insert(&cfs_runqueue, t);
#endif
}

//DO NOT MODIFY THIS FUNCTION
/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long now_us = (unsigned long)now.tv_sec * 1000000UL + (unsigned long)now.tv_usec;

    // Step 1
    if (curr) {
        unsigned long elapsed_us = (now.tv_sec - curr->last_start_time.tv_sec) * 1000000UL + (now.tv_usec - curr->last_start_time.tv_usec);
        
        curr->run_time_us += elapsed_us;

        if (curr->status == T_PREEMPTED || curr->status == T_READY) {
            curr->status = T_READY;
            rq_enqueue(curr);
        }
        
        curr = NULL;
    }

    // Step 2
    if (run_queue.length == 0 || run_queue.head == NULL) {
        // stop timer
        struct itimerval timer = {0};
        setitimer(ITIMER_PROF, &timer, NULL);

        sigprocmask(SIG_SETMASK, &oldset, NULL);
        setcontext(&main_context);
        return;
    }

    // Step 3
    tcb *prev = NULL, *iter = run_queue.head;
    tcb *min_prev = NULL, *min_node = run_queue.head;
    unsigned long min_run = min_node->run_time_us;

    while (iter) {
        if (iter->run_time_us < min_run) {
            min_run = iter->run_time_us;
            min_node = iter;
            min_prev = prev;
        }
        prev = iter;
        iter = iter->rq_next;
    }

    // Step 4
    if (min_prev == NULL) {
        // min_node is head
        run_queue.head = min_node->rq_next;
    } else {
        min_prev->rq_next = min_node->rq_next;
    }
    if (min_node == run_queue.tail) run_queue.tail = min_prev;
    min_node->rq_next = NULL;
    run_queue.length--;

    // Step 5
    curr = min_node;
    curr->status = T_RUNNING;
    tot_cntx_switches++;

    // set timer for one quantum (QUANTUM is in ms)
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    // convert QUANTUM ms to sec/usec
    timer.it_value.tv_sec  = QUANTUM / 1000;
    timer.it_value.tv_usec = (QUANTUM % 1000) * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);

    // mark start time for accounting & response time capture
    gettimeofday(&curr->last_start_time, NULL);
    if (!curr->has_run_before) {
        curr->first_run_time = curr->last_start_time;
        curr->has_run_before = true;
    }

    sigprocmask(SIG_SETMASK, &oldset, NULL);

    setcontext(&curr->context);
}

//DO NOT MODIFY THIS FUNCTION
/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// - your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	/* Step-by-step guidances */
	// Step1: Calculate the time current thread actually ran
	// Step2.1: If current thread uses up its allotment, demote it to the low priority queue (Rule 4)
	// Step2.2: Otherwise, push the thread back to its origin queue
	// Step3: If time period S passes, promote all threads to the topmost queue (Rule 5)
	// Step4: Apply RR on the topmost queue with entries and run next thread

    sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long now_us = (unsigned long)now.tv_sec * 1000000UL + (unsigned long)now.tv_usec;

    // Step 1
    if (curr) {
        unsigned long elapsed_us = (now.tv_sec - quantum_start_time.tv_sec) * 1000000UL + (now.tv_usec - quantum_start_time.tv_usec);
        
        curr->quantum_allotment_us += elapsed_us;
        
        if (curr->status == T_PREEMPTED) {
            unsigned long allotment_for_level_us = mlfq_time_slice_ms[curr->priority] * 1000UL;

            if (curr->quantum_allotment_us >= allotment_for_level_us) {
                if (curr->priority < NUM_LEVELS - 1) {
                    curr->priority++;
                }
                curr->quantum_allotment_us = 0;
            }
            
            curr->status = T_READY;
            scheduler_enqueue(curr);
            
        } else if (curr->status == T_READY) {
            scheduler_enqueue(curr);
        } else if (curr->status == T_BLOCKED) {
             curr->quantum_allotment_us = 0;
        }
        
        curr = NULL;
    }

    // Step 3
    unsigned long last_boost_us = (unsigned long)last_boost_time.tv_sec * 1000000UL + (unsigned long)last_boost_time.tv_usec;
    if (now_us - last_boost_us >= (unsigned long)PRIORITY_BOOST_S * 1000000UL) {
        for (int lvl = 1; lvl < NUM_LEVELS; ++lvl) {
            tcb *t = NULL;
            while ((t = mlfq_dequeue_level(lvl)) != NULL) {
                t->priority = 0;
                t->quantum_allotment_us = 0;
                mlfq_enqueue(t);
            }
        }
        gettimeofday(&last_boost_time, NULL);
    }

    // Step 4
    tcb *next_thread = NULL;
    int next_level = -1;
    for (int i = 0; i < NUM_LEVELS; ++i) {
        if (mlfq_queues[i].head != NULL) {
            next_thread = mlfq_dequeue_level(i);
            next_level = i;
            break;
        }
    }

    if (next_thread == NULL) {
        struct itimerval timer_off = {0};
        setitimer(ITIMER_PROF, &timer_off, NULL);
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        setcontext(&main_context);
        return;
    }

    curr = next_thread;
    curr->status = T_RUNNING;
    tot_cntx_switches++;

    gettimeofday(&quantum_start_time, NULL);
    curr->last_start_time = quantum_start_time;
    if (!curr->has_run_before) {
        curr->first_run_time = quantum_start_time;
        curr->has_run_before = true;
    }

    unsigned long time_slice_us = mlfq_time_slice_ms[curr->priority] * 1000UL;
    struct itimerval next_timer;
    next_timer.it_value.tv_sec = time_slice_us / 1000000UL;
    next_timer.it_value.tv_usec = time_slice_us % 1000000UL;
    next_timer.it_interval.tv_sec = 0;  // One-shot timer
    next_timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_PROF, &next_timer, NULL);

    sigprocmask(SIG_SETMASK, &oldset, NULL); // Restore signal mask before switching
    setcontext(&curr->context);
    perror("setcontext failed in sched_mlfq");
    abort();
}

//DO NOT MODIFY THIS FUNCTION
/* Completely fair scheduling algorithm */
static void sched_cfs(){
	// - your own implementation of CFS
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE

	/* Step-by-step guidances */

	// Step1: Update current thread's vruntime by adding the time it actually ran
	// Step2: Insert current thread into the runqueue (min heap)
	// Step3: Pop the runqueue to get the thread with a minimum vruntime
	// Step4: Calculate time slice based on target_latency (TARGET_LATENCY), number of threads within the runqueue
	// Step5: If the ideal time slice is smaller than minimum_granularity (MIN_SCHED_GRN), use MIN_SCHED_GRN instead
	// Step5: Setup next time interrupt based on the time slice
	// Step6: Run the selected thread

    sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);

    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long elapsed_us = 0;

    // Step 1
    if (curr) {
        struct timeval now;
        gettimeofday(&now, NULL);
        
        unsigned long elapsed_us = (now.tv_sec - quantum_start_time.tv_sec) * 1000000UL + (now.tv_usec - quantum_start_time.tv_usec);
        
        curr->run_time_us += elapsed_us;
        curr->vruntime_us += elapsed_us;

        // Step 2
        if (curr->status == T_PREEMPTED || curr->status == T_READY) {
            curr->status = T_READY;
            scheduler_enqueue(curr);
        }
        
        curr = NULL;
    }

    // Step 3
    tcb *next_thread = cfs_heap_pop_min(&cfs_runqueue);

    if (next_thread == NULL) {
        struct itimerval timer_off = {0};
        setitimer(ITIMER_PROF, &timer_off, NULL);
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        setcontext(&main_context);
        return;
    }

    curr = next_thread;
    curr->status = T_RUNNING;
    tot_cntx_switches++;

    gettimeofday(&quantum_start_time, NULL);
    curr->last_start_time = quantum_start_time;
    if (!curr->has_run_before) {
        curr->first_run_time = quantum_start_time;
        curr->has_run_before = true;
    }

    // Step 4 and 5
    int num_runnable = cfs_heap_size(&cfs_runqueue) + 1; // +1 for the thread we just popped
    unsigned long target_latency_us = TARGET_LATENCY * 1000UL; // Convert ms to us
    unsigned long min_granularity_us = MIN_SCHED_GRN * 1000UL; // Convert ms to us

    unsigned long time_slice_us = target_latency_us / num_runnable; [cite: 189]

    if (time_slice_us < min_granularity_us) { [cite: 191]
        time_slice_us = min_granularity_us; [cite: 192]
    }

    // Step 6
    struct itimerval next_timer;
    next_timer.it_value.tv_sec = time_slice_us / 1000000UL;
    next_timer.it_value.tv_usec = time_slice_us % 1000000UL;
    next_timer.it_interval.tv_sec = 0;
    next_timer.it_interval.tv_usec = 0;

    setitimer(ITIMER_PROF, &next_timer, NULL);

    sigprocmask(SIG_SETMASK, &oldset, NULL);
    setcontext(&curr->context);
    perror("setcontext failed in sched_cfs");
    abort();
}

/* =========================================================================
 * Trampoline
 * ========================================================================= */

// ===> File-local (helper) functions <===
static void _trampoline(void) {
	// Never returns 
	void *ret = curr->start_routine(curr->start_arg);
	worker_exit(ret);
}

/* =========================================================================
 * Stats
 * ========================================================================= */

//DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void) {

       fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
       fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
       fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}
