#ifndef REI_THREAD_H
#define REI_THREAD_H

#include "rei_types.h"

typedef void (* rei_thread_task_action_f) (void* arg);

typedef struct rei_thread_task_t {
  rei_thread_task_action_f action;
  void* arg;
  struct rei_thread_task_t* next;
} rei_thread_task_t;

typedef struct rei_thread_pool_t {
  struct {
    rei_thread_task_t* head;
    rei_thread_task_t* tail;
    pthread_mutex_t* mutex;
  } task_queue;

  pthread_cond_t* has_work_cond;
  pthread_cond_t* no_work_cond;
  u64 thread_count;
  u32 work_thread_count;
  b32 has_to_quit;
} rei_thread_pool_t;

void rei_thread_pool_create (rei_thread_pool_t* out);
void rei_thread_pool_destroy (rei_thread_pool_t* thread_pool);

void rei_thread_pool_add_task (rei_thread_pool_t* thread_pool, rei_thread_task_action_f action, void* restrict arg);
void rei_thread_pool_wait_all (rei_thread_pool_t* thread_pool);

#endif /* REI_THREAD_H */
