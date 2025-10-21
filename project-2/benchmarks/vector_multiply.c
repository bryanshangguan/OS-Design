#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "../thread-worker.h"

#define DEFAULT_THREAD_NUM 4
#define VECTOR_SIZE 3000000

/* Global variables */
pthread_mutex_t   mutex;
int thread_num;
int* counter;
pthread_t *thread;
int r[VECTOR_SIZE];
int s[VECTOR_SIZE];
static long long sum = 0;  /* use 64-bit to avoid overflow on large dot-products */

/* A CPU-bound task to do vector multiplication */
void *vector_multiply(void* arg) {
    int n = *((int*) arg);

    /* accumulate locally to minimize lock contention */
    long long local_sum = 0;

    for (int i = n; i < VECTOR_SIZE; i += thread_num) {
        local_sum += (long long)r[i] * (long long)s[i];
    }

    /* single critical section */
    pthread_mutex_lock(&mutex);
    sum += local_sum;
    pthread_mutex_unlock(&mutex);

    return NULL;
}

static long long verify(void) {

    long long verified = 0;

    for (int i = 0; i < VECTOR_SIZE; i += 1) {
        verified += (long long)r[i] * (long long)s[i];
    }
    /* if you want to print it for debugging:
       printf("verified sum is: %lld\n", verified);
    */
   return verified;
}

int main(int argc, char **argv) {

    /* robust argument parsing */
    if (argc < 2) {
        thread_num = DEFAULT_THREAD_NUM;
    } else {
        int n = atoi(argv[1]);
        if (n < 1) {
            printf("enter a valid thread number\n");
            return 1;
        }
        thread_num = n;
    }

    /* initialize counter */
    counter = (int*)malloc(thread_num*sizeof(int));
    for (int i = 0; i < thread_num; ++i)
        counter[i] = i;

    /* initialize pthread_t */
    thread = (pthread_t*)malloc(thread_num*sizeof(pthread_t));

    /* initialize data array */
    for (int i = 0; i < VECTOR_SIZE; ++i) {
        r[i] = i;
        s[i] = i;
    }

    pthread_mutex_init(&mutex, NULL);

    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);

    for (int i = 0; i < thread_num; ++i)
        pthread_create(&thread[i], NULL, vector_multiply, &counter[i]);

    for (int i = 0; i < thread_num; ++i)
        pthread_join(thread[i], NULL);

    fprintf(stderr, "***************************\n");

    clock_gettime(CLOCK_REALTIME, &end);

    long sec  = (long)(end.tv_sec - start.tv_sec);
    long nsec = (long)(end.tv_nsec - start.tv_nsec);
    long usec = sec * 1000000L + nsec / 1000L;
    printf("Total run time: %ld micro-seconds\n", usec);

    
    long long verified = verify();
	printf("Verified sum is: %lld\n", verified);


#ifdef USE_WORKERS
    fprintf(stderr , "Total sum is: %lld\n", sum);
    print_app_stats();
    fprintf(stderr, "***************************\n");
#endif
	/* Free memory on Heap */
	pthread_mutex_destroy(&mutex);
    free(thread);
    free(counter);

    return 0;
}
