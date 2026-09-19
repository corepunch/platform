#include "ios_local.h"
#include <pthread.h>
#import <objc/runtime.h>
static char ios_timer_owner;

typedef struct ios_message {
  struct AXmessage value;
  struct ios_message *next;
} ios_message_t;
static ios_message_t *ios_head, *ios_tail;
static pthread_mutex_t ios_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static NSMutableDictionary<NSNumber *, NSTimer *> *ios_timers;
static uint32_t ios_next_timer = 1;

static void ios_enqueue(struct AXmessage value) {
  void *target = value.target;
  uint32_t event = value.message, wparam = value.wParam;
  void *lparam = value.lParam;
  pthread_mutex_lock(&ios_queue_mutex);
  if (event == kEventWindowPaint || event == kEventWindowResized) {
    for (ios_message_t *p = ios_head; p; p = p->next) {
      if (p->value.target == target && p->value.message == event) {
        p->value.wParam = wparam;
        p->value.lParam = lparam;
        pthread_mutex_unlock(&ios_queue_mutex);
        return;
      }
    }
  }
  ios_message_t *p = calloc(1, sizeof(*p));
  if (!p) {
    pthread_mutex_unlock(&ios_queue_mutex);
    IOS_TRACE("queue allocation failed target=%p event=%u", target, event);
    return;
  }
  p->value = value;
  if (ios_tail) ios_tail->next = p; else ios_head = p;
  ios_tail = p;
  pthread_mutex_unlock(&ios_queue_mutex);
  CFRunLoopWakeUp(CFRunLoopGetMain());
}

void axPostMessageW(void *target, uint32_t event, uint32_t wparam, void *lparam) {
  ios_enqueue((struct AXmessage){ .target = target, .message = event, .wParam = wparam, .lParam = lparam });
}

void ios_post_gesture(ax_gesture_t gesture) {
  ios_enqueue((struct AXmessage){ .message = kEventGesture, .gesture = gesture });
}

void ios_post_touch(uint32_t event, uint32_t wparam, void *lparam, ax_pointer_t pointer) {
  ios_enqueue((struct AXmessage){
    .message = event, .wParam = wparam, .lParam = lparam, .pointer = pointer
  });
}

int axPeekMessage(struct AXmessage *msg) {
  if (!msg) { IOS_TRACE("peek rejected: null message"); return 0; }
  pthread_mutex_lock(&ios_queue_mutex);
  ios_message_t *p = ios_head;
  int ready = p != NULL;
  if (p) {
    *msg = p->value;
    ios_head = p->next;
    if (!ios_head) ios_tail = NULL;
    free(p);
  }
  pthread_mutex_unlock(&ios_queue_mutex);
  return ready;
}

// UIKit owns the main loop; frame callbacks must never block waiting for input.
int axGetMessage(struct AXmessage *msg) { return axPeekMessage(msg); }
int axWaitMessage(longTime_t msec) {
  do {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, msec ? msec / 1000.0 : 0.01, true);
  } while (UIApplication.sharedApplication.applicationState == UIApplicationStateBackground);
  pthread_mutex_lock(&ios_queue_mutex);
  int ready = ios_head != NULL;
  pthread_mutex_unlock(&ios_queue_mutex);
  return ready;
}

void axRemoveFromQueue(void *target) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_message_t **link = &ios_head;
  ios_tail = NULL;
  while (*link) {
    ios_message_t *p = *link;
    if (p->value.target == target) { *link = p->next; free(p); }
    else { ios_tail = p; link = &p->next; }
  }
  pthread_mutex_unlock(&ios_queue_mutex);
  for (NSNumber *key in [ios_timers.allKeys copy]) {
    if ([objc_getAssociatedObject(ios_timers[key], &ios_timer_owner) pointerValue] == target) axCancelTimer(key.unsignedIntValue);
  }
}

void axNotifyFileDropEvent(const char *path, float x, float y) {
  char *copy = path && *path ? strdup(path) : NULL;
  if (!copy) { IOS_TRACE("file drop rejected path=%s", path ? path : "(null)"); return; }
  IOS_TRACE("file drop path=%s", path);
  axPostMessageW(NULL, kEventDragDrop, MAKEDWORD((int)x, (int)y), copy);
}

uint32_t axSetTimer(void *target, uint32_t interval, void *userdata, bool_t repeat) {
  if (!interval || ![NSThread isMainThread]) { IOS_TRACE("timer rejected target=%p interval=%u", target, interval); return 0; }
  if (!ios_timers) ios_timers = [NSMutableDictionary new];
  uint32_t tid = ios_next_timer++;
  NSTimer *timer = [NSTimer timerWithTimeInterval:interval / 1000.0 repeats:repeat block:^(NSTimer *t) {
    (void)t;
    axPostMessageW(target, kEventTimer, tid, userdata);
    if (!repeat) [ios_timers removeObjectForKey:@(tid)];
  }];
  // Track owners separately through an associated value to cancel on destruction.
  objc_setAssociatedObject(timer, &ios_timer_owner, [NSValue valueWithPointer:target], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  ios_timers[@(tid)] = timer;
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
  return tid;
}
void axCancelTimer(uint32_t tid) { [ios_timers[@(tid)] invalidate]; [ios_timers removeObjectForKey:@(tid)]; }
void ios_cancel_timers(void) { for (NSTimer *t in ios_timers.allValues) [t invalidate]; [ios_timers removeAllObjects]; }
