#include <EGL/egl.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "qnx_local.h"

extern EGLDisplay egl_display;
extern EGLContext egl_ctx;
extern EGLSurface egl_surface;

extern void  BeginPaint(HWND hWnd);
extern void  EndPaint(HWND hWnd, LPMETRICS lpMetrics);
extern float GetWindowScale(HWND hWnd);
extern void  GetWindowSize(HWND hWnd, LPSIZE2 lpSize);

/*
 * Event queue with blocking wait support
 */

static struct
{
  struct WI_Message data[0x10000];
  uint16_t read, write;
} queue = { 0 };

static sem_t event_sem;

static void
init_event_sem(void)
{
  static int initialized = 0;
  if (!initialized) {
    sem_init(&event_sem, 0, 0);
    initialized = 1;
  }
}

void
WI_PostMessageW(void *hobj, uint32_t event, uint32_t wparam, void *lparam)
{
  init_event_sem();

  /* Coalesce duplicate resize/paint events */
  for (uint16_t r = queue.read; r != queue.write; r++) {
    if (queue.data[r].message != event)
      continue;
    switch (event) {
      case kEventWindowResized:
      case kEventWindowPaint:
        queue.data[r].target = hobj;
        queue.data[r].wParam = wparam;
        queue.data[r].lParam = lparam;
        return;
    }
  }

  queue.data[queue.write].target  = hobj;
  queue.data[queue.write].message = event;
  queue.data[queue.write].wParam  = wparam;
  queue.data[queue.write].lParam  = lparam;
  queue.write++;

  sem_post(&event_sem);
}

int
WI_PollEvent(struct WI_Message *e)
{
  if (queue.read == queue.write)
    return 0;
  *e = queue.data[queue.read++];
  return 1;
}

void
WI_RemoveFromQueue(void *target)
{
  for (uint16_t r = queue.read; r != queue.write; r++)
    if (queue.data[r].target == target)
      memset(&queue.data[r], 0, sizeof(queue.data[r]));
}

int
WI_WaitEvent(longTime_t msec)
{
  init_event_sem();

  if (msec > 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += msec / 1000;
    ts.tv_nsec += (msec % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000;
    }
    return sem_timedwait(&event_sem, &ts) == 0 ? 1 : 0;
  }

  sem_wait(&event_sem);
  return 1;
}

void
NotifyFileDropEvent(char const *filename, float x, float y)
{
  (void)filename;
  (void)x;
  (void)y;
}

/*
 * File dialogs (not supported on QNX)
 */

bool_t
WI_GetOpenFileName(WI_OpenFileName const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
WI_GetSaveFileName(WI_OpenFileName const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
WI_GetFolderName(WI_OpenFileName const *ofn)
{
  (void)ofn;
  return FALSE;
}

/*
 * Timing
 */

longTime_t
WI_GetMilliseconds(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (longTime_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

void
WI_Sleep(longTime_t msec)
{
  struct timespec ts;
  ts.tv_sec  = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

/*
 * Theme / system info
 */

bool_t
WI_IsDarkTheme(void)
{
  return FALSE;
}

/*
 * Window operations
 */

bool_t
WI_CreateSurface(uint32_t width, uint32_t height)
{
  (void)width;
  (void)height;
  return FALSE;
}

float
WI_GetScaling(void)
{
  return GetWindowScale(NULL);
}

bool_t
WI_SetSize(uint32_t width, uint32_t height, bool_t centered)
{
  (void)width;
  (void)height;
  (void)centered;
  return FALSE;
}

uint32_t
WI_GetSize(struct WI_Size *pSize)
{
  SIZE2 size = { 0, 0 };
  GetWindowSize(NULL, &size);
  if (pSize) {
    pSize->width  = (uint32_t)size.width;
    pSize->height = (uint32_t)size.height;
  }
  return MAKEDWORD(size.width, size.height);
}

void
WI_MakeCurrentContext(void)
{
  BeginPaint(NULL);
}

void
WI_BeginPaint(void)
{
  BeginPaint(NULL);
}

void
WI_EndPaint(void)
{
  EndPaint(NULL, NULL);
}

void
WI_BindFramebuffer(void)
{
  /* EGL uses the default framebuffer (0) */
}

/*
 * Key names
 */

char const *
WI_KeynumToString(uint32_t keynum)
{
  static char tinystr[2];

  if (keynum == (uint32_t)-1)
    return "<KEY NOT FOUND>";

  keynum = keynum & 0xff;

  if (keynum > 32 && keynum < 127) {
    tinystr[0] = (char)keynum;
    tinystr[1] = 0;
    return tinystr;
  }

  return "<UNKNOWN KEYNUM>";
}
