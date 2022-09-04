#include <unistd.h>
#include <memory.h>
#include <pthread.h>

#include "rei_thread.h"

static void* _s_thread_routine (void* arg) {
  rei_thread_pool_t* thread_pool = (rei_thread_pool_t*) arg;

  for (;;) {
    pthread_mutex_lock (thread_pool->task_queue.mutex);
    rei_thread_task_t* current_task = NULL;

    if (thread_pool->task_queue.head) {
      current_task = thread_pool->task_queue.head;
      thread_pool->task_queue.head = thread_pool->task_queue.head->next;
    } else {
      pthread_cond_wait (thread_pool->has_work_cond, thread_pool->task_queue.mutex);
    }

    if (thread_pool->has_to_quit) break;

    ++thread_pool->work_thread_count;
    pthread_mutex_unlock (thread_pool->task_queue.mutex);

    if (current_task) {
      current_task->action (current_task->arg);
      free (current_task);
    }

    pthread_mutex_lock (thread_pool->task_queue.mutex);
    --thread_pool->work_thread_count;
    if (!thread_pool->work_thread_count || !thread_pool->task_queue.head) pthread_cond_signal (thread_pool->no_work_cond);
    pthread_mutex_unlock (thread_pool->task_queue.mutex);
  }

  pthread_cond_signal (thread_pool->has_work_cond);
  pthread_mutex_unlock (thread_pool->task_queue.mutex);

  return NULL;
}

void rei_thread_pool_create (rei_thread_pool_t* out) {
  out->task_queue.head = NULL;
  out->task_queue.tail = NULL;
  out->task_queue.mutex = malloc (sizeof *out->task_queue.mutex);
  out->has_work_cond = malloc (sizeof *out->has_work_cond);
  out->no_work_cond = malloc (sizeof *out->no_work_cond);

  pthread_mutex_init (out->task_queue.mutex, NULL);
  pthread_cond_init (out->has_work_cond, NULL);
  pthread_cond_init (out->no_work_cond, NULL);

  out->work_thread_count = 0;
  out->has_to_quit = REI_FALSE;
  const s64 thread_count = sysconf (_SC_NPROCESSORS_ONLN) * 2;

  pthread_t thread;

  for (s64 i = 0; i < thread_count; ++i) {
    pthread_create (&thread, NULL, _s_thread_routine, out);
    pthread_detach (thread);
  }
}

void rei_thread_pool_destroy (rei_thread_pool_t* thread_pool) {
  pthread_mutex_lock (thread_pool->task_queue.mutex);

  thread_pool->has_to_quit = REI_TRUE;

  rei_thread_task_t* current = thread_pool->task_queue.head;

  while (current) {
    rei_thread_task_t* tmp = current;
    current = current->next;

    free (tmp);
  }

  pthread_cond_broadcast (thread_pool->has_work_cond);
  pthread_mutex_unlock (thread_pool->task_queue.mutex);

  rei_thread_pool_wait_all (thread_pool);

  pthread_cond_destroy (thread_pool->no_work_cond);
  pthread_cond_destroy (thread_pool->has_work_cond);
  pthread_mutex_destroy (thread_pool->task_queue.mutex);

  free (thread_pool->no_work_cond);
  free (thread_pool->has_work_cond);
  free (thread_pool->task_queue.mutex);
}

void rei_thread_pool_add_task (rei_thread_pool_t* thread_pool, rei_thread_task_action_f action, void* restrict arg) {
  rei_thread_task_t* new_task = malloc (sizeof *new_task);
  new_task->action = action;
  new_task->arg = arg;
  new_task->next = NULL;

  pthread_mutex_lock (thread_pool->task_queue.mutex);

  if (thread_pool->task_queue.head) {
    thread_pool->task_queue.tail->next = new_task;
  } else {
    thread_pool->task_queue.head = new_task;
  }

  thread_pool->task_queue.tail = new_task;

  pthread_cond_broadcast (thread_pool->has_work_cond);
  pthread_mutex_unlock (thread_pool->task_queue.mutex);
}

void rei_thread_pool_wait_all (rei_thread_pool_t* thread_pool) {
  pthread_mutex_lock (thread_pool->task_queue.mutex);

  for (;;) {
    if (thread_pool->work_thread_count || thread_pool->task_queue.head) {
      pthread_cond_wait (thread_pool->no_work_cond, thread_pool->task_queue.mutex);
    } else {
      break;
    }
  }

  pthread_mutex_unlock (thread_pool->task_queue.mutex);
}
