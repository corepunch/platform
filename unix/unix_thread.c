#include <pthread.h>
#include <stdlib.h>

#include "../platform.h"

struct thread_ctx {
  void (*fn)(void *);
  void *arg;
};

static void *thread_trampoline(void *p)
{
  struct thread_ctx ctx = *(struct thread_ctx *)p;
  free(p);
  ctx.fn(ctx.arg);
  return NULL;
}

axthread_t
axThreadCreate(void (*fn)(void *), void *arg)
{
  struct thread_ctx *ctx = malloc(sizeof(*ctx));
  if (!ctx) return NULL;
  ctx->fn  = fn;
  ctx->arg = arg;

  pthread_t *t = malloc(sizeof(pthread_t));
  if (!t) { free(ctx); return NULL; }

  if (pthread_create(t, NULL, thread_trampoline, ctx) != 0) {
    free(ctx);
    free(t);
    return NULL;
  }
  return t;
}

void
axThreadJoin(axthread_t thread)
{
  if (!thread) return;
  pthread_join(*(pthread_t *)thread, NULL);
  free(thread);
}

axmutex_t
axMutexCreate(void)
{
  pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
  if (!m) return NULL;
  if (pthread_mutex_init(m, NULL) != 0) { free(m); return NULL; }
  return m;
}

void
axMutexLock(axmutex_t mutex)
{
  pthread_mutex_lock((pthread_mutex_t *)mutex);
}

void
axMutexUnlock(axmutex_t mutex)
{
  pthread_mutex_unlock((pthread_mutex_t *)mutex);
}

void
axMutexDestroy(axmutex_t mutex)
{
  if (!mutex) return;
  pthread_mutex_destroy((pthread_mutex_t *)mutex);
  free(mutex);
}
