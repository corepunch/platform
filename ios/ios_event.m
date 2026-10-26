#include "ios_local.h"
#import <objc/runtime.h>
static char ios_timer_owner;
static NSMutableDictionary<NSNumber *, NSTimer *> *ios_timers;
static uint32_t ios_next_timer = 1;

void ios_post_gesture(ax_gesture_t gesture) {
  ios_enqueue((struct AXmessage){ .message = kEventGesture, .gesture = gesture });
}

void ios_post_touch(uint32_t event, uint32_t wparam, void *lparam, ax_pointer_t pointer) {
  ios_enqueue((struct AXmessage){
    .message = event, .wParam = wparam, .lParam = lparam, .pointer = pointer
  });
}

// UIKit owns the main loop; frame callbacks must never block waiting for input.
int axGetMessage(struct AXmessage *msg) { return axPeekMessage(msg); }
int axWaitMessage(longTime_t msec) {
  // ORCA frame polling cannot reenter UIKit; standalone modal loops still pump it.
  if (msec || !ios_external_lifecycle())
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, msec ? msec / 1000.0 : 0.01, true);
  return ios_messages_ready();
}

void axRemoveFromQueue(void *target) {
  ios_remove_messages(target);
  for (NSNumber *key in ios_timers.allKeys) {
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
  while (!ios_next_timer || ios_timers[@(ios_next_timer)]) ios_next_timer++;
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

void axIOSShutdownEvents(void) {
  ios_cancel_timers();
#if !__has_feature(objc_arc)
  [ios_timers release];
#endif
  ios_timers = nil;
  ios_shutdown_messages();
}
