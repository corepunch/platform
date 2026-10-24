#include "ios_local.h"
#include <pthread.h>

struct queued_message {
  struct AXmessage message;
  struct queued_message *next;
};

static struct queued_message *first, *last;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t next_message_id = 1, next_timer_id = 1;
static struct {
  uint32_t id;
  void *target;
  NSTimer *timer;
} timers[64];

void
axPostMessageW(void *target, uint32_t event, uint32_t wparam, void *lparam)
{
  pthread_mutex_lock(&queue_lock);
  if (event == kEventWindowPaint || event == kEventWindowResized) {
    for (struct queued_message *q = first; q; q = q->next) {
      if (q->message.message != event || q->message.target != target) continue;
      q->message.wParam = wparam;
      q->message.lParam = lparam;
      pthread_mutex_unlock(&queue_lock);
      return;
    }
  }
  struct queued_message *q = calloc(1, sizeof(*q));
  if (q) {
    q->message = (struct AXmessage){
      .target = target, .message = event, .wParam = wparam,
      .lParam = lparam, .id = next_message_id++
    };
    if (last) last->next = q;
    else first = q;
    last = q;
  }
  pthread_mutex_unlock(&queue_lock);
}

int
axPeekMessage(struct AXmessage *msg)
{
  if (!msg) return 0;
  pthread_mutex_lock(&queue_lock);
  struct queued_message *q = first;
  if (q) {
    *msg = q->message;
    first = q->next;
    if (!first) last = NULL;
    free(q);
  }
  pthread_mutex_unlock(&queue_lock);
  return q != NULL;
}

int axGetMessage(struct AXmessage *msg) { return axPeekMessage(msg); }

int
axWaitMessage(longTime_t msec)
{
  (void)msec;
  /* UIKit owns the main loop; the host resumes the engine each display tick. */
  pthread_mutex_lock(&queue_lock);
  int ready = first != NULL;
  pthread_mutex_unlock(&queue_lock);
  return ready;
}

void
axRemoveFromQueue(void *target)
{
  pthread_mutex_lock(&queue_lock);
  struct queued_message **link = &first;
  last = NULL;
  while (*link) {
    struct queued_message *q = *link;
    if (q->message.target == target) {
      *link = q->next;
      if (q->message.message == kEventDragDrop) free(q->message.lParam);
      free(q);
    } else {
      last = q;
      link = &q->next;
    }
  }
  pthread_mutex_unlock(&queue_lock);
  for (size_t i = 0; i < sizeof(timers) / sizeof(*timers); i++)
    if (timers[i].id && timers[i].target == target) axCancelTimer(timers[i].id);
}

void
axNotifyFileDropEvent(char const *filename, float x, float y)
{
  if (!filename || !*filename) return;
  char *path = strdup(filename);
  if (path) axPostMessageW(NULL, kEventDragDrop, MAKEDWORD((int)x, (int)y), path);
}

uint32_t
axSetTimer(void *target, uint32_t interval_ms, void *userdata, bool_t repeat)
{
  for (size_t i = 0; i < sizeof(timers) / sizeof(*timers); i++) {
    if (timers[i].id) continue;
    uint32_t id = next_timer_id++;
    if (!id) id = next_timer_id++;
    timers[i].id = id;
    timers[i].target = target;
    timers[i].timer = [[NSTimer timerWithTimeInterval:MAX(interval_ms, 1) / 1000.0
                                            repeats:repeat != 0
                                              block:^(NSTimer *timer) {
      (void)timer;
      axPostMessageW(target, kEventTimer, id, userdata);
      if (!repeat) axCancelTimer(id);
    }] retain];
    [[NSRunLoop mainRunLoop] addTimer:timers[i].timer forMode:NSRunLoopCommonModes];
    return id;
  }
  return 0;
}

void
axCancelTimer(uint32_t id)
{
  if (!id) return;
  for (size_t i = 0; i < sizeof(timers) / sizeof(*timers); i++) {
    if (timers[i].id != id) continue;
    [timers[i].timer invalidate];
    [timers[i].timer release];
    memset(&timers[i], 0, sizeof(timers[i]));
    return;
  }
}

void
axIOSShutdownEvents(void)
{
  for (size_t i = 0; i < sizeof(timers) / sizeof(*timers); i++)
    if (timers[i].id) axCancelTimer(timers[i].id);
  pthread_mutex_lock(&queue_lock);
  while (first) {
    struct queued_message *q = first;
    first = q->next;
    if (q->message.message == kEventDragDrop) free(q->message.lParam);
    free(q);
  }
  last = NULL;
  pthread_mutex_unlock(&queue_lock);
}
