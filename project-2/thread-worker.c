// File:  thread-worker.c
// List all group member's name: Kelvin Ihezue,...
// username of iLab: ki120
// iLab Server:

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

/* mutex wait-queue */
static inline void mutex_enqueue(worker_mutex_t *m, tcb *t);
static inline tcb* mutex_dequeue(worker_mutex_t *m);

/* utility for timers */
static inline void convert_ms_itimerval(int ms, struct timeval *t_val);

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

    // Preempt the running thread:
    curr->status = T_READY;

    // Put the preempted thread at the tail so others get a chance
    rq_enqueue(curr);

    tcb *me = curr;
    curr = NULL;

    swapcontext(&me->context, &scheduler_context);
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

	// Needed this to run part 1
	if (curr && curr->status == T_RUNNING) {
        curr->status = T_READY;
        rq_enqueue(curr);
    }
    tcb *next = rq_dequeue();
    if (!next) setcontext(&main_context);
    next->status = T_RUNNING;
    curr = next;
    tot_cntx_switches++;
    setcontext(&curr->context);

	// - invoke scheduling algorithms according to the policy (PSJF or MLFQ or CFS)
#if defined(PSJF)
    	sched_psjf();
#elif defined(MLFQ)
	sched_mlfq(/* ... */);
#elif defined(CFS)
    	sched_cfs(/* ... */);  
#else
	# error "Define one of PSJF, MLFQ, or CFS when compiling. e.g. make SCHED=MLFQ"
#endif
}

/* =========================================================================
 * Public API
 * ========================================================================= */

// Part 1: 1.1
/* create a new thread */
int worker_create(worker_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

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

		// init runqueue (1.1.4)
        rq_init(&run_queue);

		// capture main context so the scheduler can return to it
        getcontext(&main_context);

		// build scheduler context + stack
		getcontext(&scheduler_context);
		void *scheduler_stack = malloc(STACK_BYTES);
		if (!scheduler_stack) {errno = ENOMEM; return -1;}

		scheduler_context.uc_stack.ss_sp = scheduler_stack;
        scheduler_context.uc_stack.ss_size = STACK_BYTES;
        scheduler_context.uc_link = &main_context;   // if scheduler falls through
        makecontext(&scheduler_context, schedule, 0);

		// 1.1.5 Timers: arm the periodic preemption timer
		t_ms_start(QUANTUM);
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

		// Register so joins/exits can find it even when not on run queue
		registry_add(t);
		
		// Inline Enqueue to push READY tcb 't' to tail of run_queue
		rq_enqueue(t); // 1.1.4

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

	if (!curr) {
		swapcontext(&main_context, &scheduler_context);
		return;
	}

	curr->status = T_READY;
	rq_enqueue(curr);

	tcb *me = curr; // me points to the same TCB as curr
	curr = NULL; 	// no current while the scheduler runs
	swapcontext(&me->context, &scheduler_context);
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

	// 2) Mark logical completion in the TCB.
	curr->is_finished = true;
	curr->status = T_COMPLETED;

	// Part 1.4: Wake the joiner if one exists
	if (me->joiner_tid) {
		tcb *j = find_tcb(me->joiner_tid);

		if (j && j->waiting_on == me->t_id && j->status == T_BLOCKED) {
			j->waiting_on = 0;
			j ->status = T_READY;
			rq_enqueue(j);  // 1.1.4
		}
	}
	
	// 4) This thread is done running; clear 'curr' so the scheduler owns the CPU.
	curr = NULL;

	swapcontext(&me->context, &scheduler_context);
	// No return
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
		if (!mutex || !mutex->init) {errno = EINVAL; return -1;}

		// main thread path
		if (!curr) {
			if (mutex->owner == 0) { mutex->owner = MAIN_OWNER; return 0; }
			while (mutex->owner != 0) {
				swapcontext(&main_context, &scheduler_context);
			}
			mutex->owner = MAIN_OWNER;
			return 0;
		}

	   	if (mutex->owner == curr->t_id) {errno = EDEADLK; return -1;} // non-recursive

		if (mutex->owner == 0) { // Fast path
			mutex->owner = curr->t_id;
			return 0;
		}

		// Slow path: already locked → block and wait our turn
		curr->status = T_BLOCKED;
		mutex_enqueue(mutex, curr);
		tcb *me = curr; curr = NULL;
		swapcontext(&me->context, &scheduler_context);

		if (mutex->owner != me->t_id) {errno = EPERM; return -1;}
        return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex) {
	// - release mutex and make it available again. 
	// - put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	if (!mutex || !mutex->init) {errno = EINVAL; return -1;}

	// main thread path
	if (!curr) {
		if (mutex->owner != MAIN_OWNER) {errno = EPERM; return -1;}
		tcb *next_owner = mutex_dequeue(mutex);
		if (next_owner) {
			mutex->owner = next_owner->t_id;
			next_owner->status = T_READY;
			rq_enqueue(next_owner);
		} else {
			mutex->owner = 0;
		}
		return 0;
	}

	// worker path
    if (mutex->owner != curr->t_id) { errno = EPERM; return -1; }
    tcb *next_owner = mutex_dequeue(mutex);
    if (next_owner) {
        mutex->owner = next_owner->t_id;
        next_owner->status = T_READY;
        rq_enqueue(next_owner);
    } else {
        mutex->owner = 0;
    }
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

//DO NOT MODIFY THIS FUNCTION
/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf() {
	// - your own implementation of PSJF
	// (feel free to modify arguments and return types)

	sigset_t oldset, blockset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGPROF);
    sigprocmask(SIG_BLOCK, &blockset, &oldset);
t
    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned long now_us = (unsigned long)now.tv_sec * 1000000UL + (unsigned long)now.tv_usec;

    // Step 1
    if (current_thread != NULL) {
        unsigned long start_us = (unsigned long)quantum_start_time.tv_sec * 1000000UL +
                                 (unsigned long)quantum_start_time.tv_usec;
        unsigned long elapsed_us = now_us - start_us;

        current_thread->run_time_us += elapsed_us;
        current_thread->status = READY;
        enqueue(current_thread);
    }

    // Step 2
    if (runqueue_head == NULL) {
        current_thread = NULL;

        // stop the timer
        struct itimerval timer = {0};
        setitimer(ITIMER_PROF, &timer, NULL);

        sigprocmask(SIG_SETMASK, &oldset, NULL);
        setcontext(&main_context);
        return;
    }

    // Step 3
    tcb *shortest = runqueue_head;
    tcb *prev = NULL;
    tcb *shortest_prev = NULL;
    for (tcb *iter = runqueue_head; iter != NULL; iter = iter->next) {
        if (iter->run_time_us < shortest->run_time_us) {
            shortest = iter;
            shortest_prev = prev;
        }
        prev = iter;
    }

    // Step 4
    if (shortest_prev == NULL) {
        runqueue_head = shortest->next;
    } else {
        shortest_prev->next = shortest->next;
    }
    shortest->next = NULL;

    // Step 5
    current_thread = shortest;
    current_thread->status = SCHEDULED;
    tot_cntx_switches++;

    // Step 6
    if (current_thread->has_run_before == 0) {
        struct timeval first_run_time;
        gettimeofday(&first_run_time, NULL);
        current_thread->first_run_time_us =
            (unsigned long)first_run_time.tv_sec * 1000000UL +
            (unsigned long)first_run_time.tv_usec;
        current_thread->has_run_before = 1;
    }

    // Step 7
#ifdef QUANTUM_US
    unsigned long slice_us = QUANTUM_US;
#else
    unsigned long slice_us = 50000UL; // default 50 ms
#endif
    struct itimerval timer;
    timer.it_value.tv_sec  = slice_us / 1000000UL;
    timer.it_value.tv_usec = slice_us % 1000000UL;
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);

    // Step 8
    gettimeofday(&quantum_start_time, NULL);

    // Step 9
    sigprocmask(SIG_SETMASK, &oldset, NULL);
    setcontext(&(current_thread->context));
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
    unsigned long elapsed_us = 0;

    // Step 1
    if (current_thread != NULL) {
        unsigned long start_us = (unsigned long)quantum_start_time.tv_sec * 1000000UL + (unsigned long)quantum_start_time.tv_usec;
        elapsed_us = now_us - start_us;

        // update the accumulated quantum usage at current priority
        current_thread->quantum_allotment_us += elapsed_us;
        current_thread->status = READY;

        // get the allotment time_slice array is expected to be in microseconds
        unsigned long allotment_for_level = time_slice[current_thread->priority];

        // Step 2.1
        if (current_thread->quantum_allotment_us >= allotment_for_level) {
            if (current_thread->priority < NUM_LEVELS - 1) {
                current_thread->priority++;
            }
            // reset the accumulated allotment for the new level
            current_thread->quantum_allotment_us = 0;
        }
        // Step2.2: push the thread back into its (possibly new) queue
        enqueue(current_thread->priority, current_thread);
    }

    // Step 3
    unsigned long last_boost_us = (unsigned long)last_boost_time.tv_sec * 1000000UL + (unsigned long)last_boost_time.tv_usec;
    if (now_us - last_boost_us >= (unsigned long)PRIORITY_BOOST_S * 1000000UL) {
        for (int lvl = 1; lvl < NUM_LEVELS; ++lvl) {
            tcb *th = dequeue(lvl);
            while (th != NULL) {
                tcb *next = dequeue(lvl); // dequeue returns head each call
                th->priority = 0;
                th->quantum_allotment_us = 0;
                enqueue(0, th);
                th = next;
            }
        }
        gettimeofday(&last_boost_time, NULL);
    }

    // Step 4
    tcb *next_thread = NULL;
    int pick_level = -1;
    for (int i = 0; i < NUM_LEVELS; ++i) {
#ifdef HAVE_PEEK_HEAD
        if (peek_head(i) != NULL) {
            next_thread = dequeue(i);
            pick_level = i;
            break;
        }
#else
        if (mlfq_queues[i] != NULL) {
            next_thread = dequeue(i);
            pick_level = i;
            break;
        }
#endif
    }

    if (next_thread == NULL) {
        current_thread = NULL;
        struct itimerval timer = {0};
        setitimer(ITIMER_PROF, &timer, NULL);
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        setcontext(&main_context);
        return;
    }

    // schedule next thread
    current_thread = next_thread;
    current_thread->status = SCHEDULED;
    tot_cntx_switches++;

    // update response time metric if first run
    if (current_thread->has_run_before == 0) {
        struct timeval first_run_time;
        gettimeofday(&first_run_time, NULL);
        current_thread->first_run_time_us = (unsigned long)first_run_time.tv_sec * 1000000UL + (unsigned long)first_run_time.tv_usec;
        current_thread->has_run_before = 1;
    }

    // either remaining allotment at this level or full allotment 
    unsigned long allotment_for_level = time_slice[current_thread->priority];
    unsigned long remaining_allotment_us = 0;

    if (current_thread->quantum_allotment_us >= allotment_for_level) {
        remaining_allotment_us = allotment_for_level;
        current_thread->quantum_allotment_us = 0;
    } else {
        remaining_allotment_us = allotment_for_level - current_thread->quantum_allotment_us;
    }

    if (remaining_allotment_us == 0) {
        remaining_allotment_us = allotment_for_level;
    }

    struct itimerval timer;
    timer.it_value.tv_sec = remaining_allotment_us / 1000000UL;
    timer.it_value.tv_usec = remaining_allotment_us % 1000000UL;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);

    gettimeofday(&quantum_start_time, NULL);
    sigprocmask(SIG_SETMASK, &oldset, NULL);
    setcontext(&(current_thread->context));
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
    if (current_thread != NULL) {
        elapsed_us = (now.tv_sec - quantum_start_time.tv_sec) * 1000000UL +
                     (now.tv_usec - quantum_start_time.tv_usec);
        current_thread->vruntime_us += elapsed_us;
        current_thread->status = READY;
        heap_insert(current_thread);
    }

    // Step 2
    tcb *next_thread = heap_pop_min();

    // Step 3
    if (next_thread == NULL) {
        current_thread = NULL;
        struct itimerval timer = {0};
        setitimer(ITIMER_PROF, &timer, NULL);
        sigprocmask(SIG_SETMASK, &oldset, NULL);
        setcontext(&main_context);
        return;
    }

    // Step 4
    current_thread = next_thread;
    current_thread->status = SCHEDULED;
    tot_cntx_switches++;

    // Step 5
    int num_runnable = heap_size() + 1;
    unsigned long target_latency_us = TARGET_LATENCY * 1000UL;
    unsigned long min_granularity_us = MIN_SCHED_GRN * 1000UL;
    unsigned long time_slice_us = target_latency_us / num_runnable;
    if (time_slice_us < min_granularity_us)
        time_slice_us = min_granularity_us;

    // Step 6
    if (current_thread->has_run_before == 0) {
        struct timeval first_run_time;
        gettimeofday(&first_run_time, NULL);
        current_thread->first_run_time_us =
            (first_run_time.tv_sec * 1000000UL) + first_run_time.tv_usec;
        current_thread->has_run_before = 1;
    }

    // Step 7
    struct itimerval timer;
    timer.it_value.tv_sec = time_slice_us / 1000000UL;
    timer.it_value.tv_usec = time_slice_us % 1000000UL;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &timer, NULL);

    // Step 8
    gettimeofday(&quantum_start_time, NULL);
    sigprocmask(SIG_SETMASK, &oldset, NULL);

    // Step 9
    setcontext(&(current_thread->context));
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
