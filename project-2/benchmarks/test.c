#include <stdio.h>
#include <stdint.h>     
#include <pthread.h>
#include "../thread-worker.h"

// /* A scratch program template on which to call and
//  * test thread-worker library functions as you implement
//  * them.
//  *
//  * You can modify and use this program as much as possible.
//  * This will not be graded.
//  */


/* ========= Basic two-thread demo (Original) ========= */
static pthread_mutex_t m;

static void* worker(void* p){
  int id = *(int*)p;
  for(int r=0; r<3; r++){
    pthread_mutex_lock(&m);
    printf("T%d in crit r=%d\n", id, r);
    pthread_mutex_unlock(&m);
    worker_yield(); // exercise cooperative switch
  }
  return (void*)(intptr_t)(id*10);
}

/* ========= Preemption smoke (no explicit yields) ========= */
static volatile int A=0, B=0;

static void* spinA(void *_) { for (int i=0; i<400000; i++) A++; return NULL; }
static void* spinB(void *_) { for (int i=0; i<400000; i++) B++; return NULL; }

static void test_preemption_smoke(void) {
  printf("\n== Preemption smoke ==\n");
  pthread_t t1, t2;
  // DO NOT call worker_yield here; timer should preempt
  pthread_create(&t1, NULL, spinA, NULL);
  pthread_create(&t2, NULL, spinB, NULL);
  pthread_join(t1, NULL);
  pthread_join(t2, NULL);
  printf("A=%d  B=%d  (expect both > 0)\n", A, B);
}

/* ========= Mutex edge cases ========= */
static pthread_mutex_t edge_m;

static void* lock_twice(void *_) {
  // first lock should succeed
  if (pthread_mutex_lock(&edge_m) != 0) {
    printf("first lock failed unexpectedly\n");
    return NULL;
  }
  // second lock should fail in your non-recursive impl (EDEADLK-like, rc=-1)
  int rc = pthread_mutex_lock(&edge_m);
  printf("second lock rc=%d (expect -1)\n", rc);
  pthread_mutex_unlock(&edge_m);
  return NULL;
}

static pthread_mutex_t mdestroy;
static void* waiter_on_destroy(void *_) {
  // this will only acquire after main unlocks
  pthread_mutex_lock(&mdestroy);
  pthread_mutex_unlock(&mdestroy);
  return NULL;
}

static void test_mutex_edges(void) {
  printf("\n== Mutex edge cases ==\n");

  // Non-recursive behavior
  pthread_mutex_init(&edge_m, NULL);
  pthread_t t; pthread_create(&t, NULL, lock_twice, NULL);
  // optional: give scheduler a nudge, but join will also drive it
  worker_yield();
  pthread_join(t, NULL);
  pthread_mutex_destroy(&edge_m);

  // Destroy semantics
  pthread_mutex_init(&mdestroy, NULL);

  // 1) Destroy while locked -> EBUSY (-1)
  pthread_mutex_lock(&mdestroy);
  int rc = pthread_mutex_destroy(&mdestroy);
  printf("destroy while locked rc=%d (expect -1/EBUSY)\n", rc);
  pthread_mutex_unlock(&mdestroy);

  // 2) Destroy with waiter -> EBUSY (-1)
  pthread_mutex_lock(&mdestroy);
  pthread_t w; pthread_create(&w, NULL, waiter_on_destroy, NULL);
  worker_yield(); // let waiter block on mdestroy
  rc = pthread_mutex_destroy(&mdestroy);
  printf("destroy with waiter rc=%d (expect -1/EBUSY)\n", rc);

  // 3) Clean up, then destroy should succeed
  pthread_mutex_unlock(&mdestroy);
  pthread_join(w, NULL);
  rc = pthread_mutex_destroy(&mdestroy);
  printf("destroy after clear rc=%d (expect 0)\n", rc);
}

int main(void) {
  
  pthread_mutex_init(&m, NULL);
  pthread_t a, b; int ia=1, ib=2;
  pthread_create(&a, NULL, worker, &ia);
  pthread_create(&b, NULL, worker, &ib);

  // kick the scheduler once from main so workers start running
  worker_yield();

  void *ra=NULL, *rb=NULL;
  pthread_join(a, &ra);
  pthread_join(b, &rb);
  printf("rets: %ld %ld\n", (long)ra, (long)rb);
  pthread_mutex_destroy(&m);

  /* ===== Added tests ===== */
  test_preemption_smoke();
  test_mutex_edges();

  print_app_stats(); 
  return 0;
}