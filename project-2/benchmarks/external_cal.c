#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <pthread.h>
#include "../thread-worker.h"

#define DEFAULT_THREAD_NUM 4
#define RAM_SIZE 160
#define RECORD_NUM 10
#define RECORD_SIZE 4194304

/* Global variables */
pthread_mutex_t   mutex;
int thread_num;
int* counter;
pthread_t *thread;
int *mem = NULL;
static long long sum = 0;            // use 64-bit accumulation
int itr = RECORD_SIZE / 16;

static void *external_calculate(void* arg) {
	int i = 0, j = 0, k = 0;
	int n = *((int*) arg);
	long long local_sum = 0;          // accumulate locally to reduce lock traffic

	for (k = n; k < RECORD_NUM; k += thread_num) {
		char a[3];
		char path[20] = "./record/";
		sprintf(a, "%d", k);
		strcat(path, a);

		FILE *f = fopen(path, "r");
		if (!f) {
			printf("failed to open file %s, please run ./genRecord.sh first\n", path);
			exit(1);
		}

		for (i = 0; i < itr; ++i) {
			// read 16B (4 ints) from kth record
			for (j = 0; j < 4; ++j) {
				fscanf(f, "%d", &mem[k*4 + j]);
				local_sum += (long long)mem[k*4 + j]; // no lock here
			}
		}
		fclose(f);
	}

	// single critical section to update global sum
	pthread_mutex_lock(&mutex);
	sum += local_sum;
	pthread_mutex_unlock(&mutex);
	return NULL;
}

static long long verify(void) {
	int i, j, k;
	char a[3], path[20];
	long long verified = 0;           // local verified total

	memset(mem, 0, RAM_SIZE);

	for (k = 0; k < 10; ++k) {
		strcpy(path, "./record/");
		sprintf(a, "%d", k);
		strcat(path, a);

		FILE *f = fopen(path, "r");
		if (!f) {
			printf("failed to open file %s, please run ./genRecord.sh first\n", path);
			exit(1);
		}

		for (i = 0; i < itr; ++i) {
			for (j = 0; j < 4; ++j) {
				fscanf(f, "%d", &mem[k*4 + j]);      // no trailing \n
				verified += (long long)mem[k*4 + j]; // add to verified (not global sum)
			}
		}
		fclose(f);
	}
	return verified;
}

void sig_handler(int signum) {
	printf("%d\n", signum);
}

int main(int argc, char **argv) {
	int i = 0;

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

	// initialize counter
	counter = (int*)malloc(thread_num*sizeof(int));
	for (i = 0; i < thread_num; ++i) counter[i] = i;

	// initialize pthread_t and memory
	thread = (pthread_t*)malloc(thread_num*sizeof(pthread_t));
	mem = (int*)malloc(RAM_SIZE);
	memset(mem, 0, RAM_SIZE);

	pthread_mutex_init(&mutex, NULL);

	struct timespec start, end;
	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < thread_num; ++i)
		pthread_create(&thread[i], NULL, external_calculate, &counter[i]);

	signal(SIGABRT, sig_handler);
	signal(SIGSEGV, sig_handler);

	for (i = 0; i < thread_num; ++i)
		pthread_join(thread[i], NULL);

	fprintf(stderr, "***************************\n");

	clock_gettime(CLOCK_REALTIME, &end);
	long sec  = (long)(end.tv_sec - start.tv_sec);
	long nsec = (long)(end.tv_nsec - start.tv_nsec);
	long usec = sec * 1000000L + nsec / 1000L;
	printf("Total run time: %ld micro-seconds\n", usec);

	pthread_mutex_destroy(&mutex);

	// verify and report
	long long verified = verify();

#ifdef USE_WORKERS
	fprintf(stderr , "Total sum is: %lld\n", sum);
	fprintf(stderr , "Verified sum is: %lld\n", verified);
	print_app_stats();
	fprintf(stderr, "***************************\n");
#endif

	free(mem);
	free(thread);
	free(counter);
	return 0;
}
