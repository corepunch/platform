#include <windows.h>
#include <process.h>
#include <stdlib.h>

#include "../platform.h"

struct thread_ctx {
  void (*fn)(void *);
  void *arg;
};

static unsigned __stdcall thread_trampoline(void *p)
{
  struct thread_ctx ctx = *(struct thread_ctx *)p;
  free(p);
  ctx.fn(ctx.arg);
  return 0;
}

axthread_t
axThreadCreate(void (*fn)(void *), void *arg)
{
  struct thread_ctx *ctx = malloc(sizeof(*ctx));
  if (!ctx) return NULL;
  ctx->fn  = fn;
  ctx->arg = arg;

  uintptr_t h = _beginthreadex(NULL, 0, thread_trampoline, ctx, 0, NULL);
  if (h == 0) { free(ctx); return NULL; }
  return (axthread_t)(HANDLE)h;
}

void
axThreadJoin(axthread_t thread)
{
  if (!thread) return;
  WaitForSingleObject((HANDLE)thread, INFINITE);
  CloseHandle((HANDLE)thread);
}

axmutex_t
axMutexCreate(void)
{
  CRITICAL_SECTION *cs = malloc(sizeof(CRITICAL_SECTION));
  if (!cs) return NULL;
  InitializeCriticalSection(cs);
  return cs;
}

void
axMutexLock(axmutex_t mutex)
{
  EnterCriticalSection((CRITICAL_SECTION *)mutex);
}

void
axMutexUnlock(axmutex_t mutex)
{
  LeaveCriticalSection((CRITICAL_SECTION *)mutex);
}

void
axMutexDestroy(axmutex_t mutex)
{
  if (!mutex) return;
  DeleteCriticalSection((CRITICAL_SECTION *)mutex);
  free(mutex);
}
