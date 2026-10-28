#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "macos_keymap.h"
#include "macos_local.h"

#define MAX_TIMERS 64
static struct {
  uint32_t  id;
  void*     obj;
  NSTimer*  timer;
  void*     userdata;
} s_timers[MAX_TIMERS];
static uint32_t s_next_timer_id = 1;

uint32_t KEY_GetKeyName(uint32_t keycode) {
  for (struct keymap const *km = darwin_scancode_table; km->keyname; km++) {
    if (keycode == km->keycode)
    {
      return km->keyname;
    }
  }
  return -1;
}

struct
{
  struct AXmessage data[0x10000];
  uint16_t read, write;
} queue = { 0 };

void
axClearMessageQueue(void)
{
  struct AXmessagePayload *dispose = NULL;
  while (queue.read != queue.write) {
    axTakeMessagePayload(&queue.data[queue.read], &dispose);
    memset(&queue.data[queue.read++], 0, sizeof(struct AXmessage));
  }
  axDisposePayloads(dispose);
}

void
axRemoveFromQueue(void *target)
{
  struct AXmessagePayload *dispose = NULL;
  for (uint16_t r = queue.read; r != queue.write; r++) {
    if (queue.data[r].target != target) continue;
    axTakeMessagePayload(&queue.data[r], &dispose);
    memset(&queue.data[r], 0, sizeof(queue.data[r]));
  }
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id != 0 && s_timers[i].obj == target)
      axCancelTimer(s_timers[i].id);
  axDisposePayloads(dispose);
}

void
axRemoveMessages(void *target, uint32_t message, uint32_t wparam)
{
  struct AXmessagePayload *dispose = NULL;
  // Cocoa events refer to queue slots by index, so clear matches in place.
  for (uint16_t r = queue.read; r != queue.write; r++) {
    struct AXmessage *e = &queue.data[r];
    if (e->target != target || e->message != message ||
        (wparam != AX_ANY_WPARAM && e->wParam != wparam)) continue;
    axTakeMessagePayload(e, &dispose);
    memset(e, 0, sizeof(*e));
  }
  axDisposePayloads(dispose);
}

void
axPostMessageOwnedW(struct AXmessage msg)
{
  if (msg.message == kEventWindowResized || msg.message == kEventWindowPaint) {
    for (uint16_t r = queue.read; r != queue.write; r++) {
      if (queue.data[r].target != msg.target || queue.data[r].message != msg.message) continue;
      struct AXmessage old = queue.data[r];
      queue.data[r] = msg;
      axDisposeMessage(&old);
      return;
    }
  }
  if ((uint16_t)(queue.write + 1) == queue.read) {
    axDisposeMessage(&msg);
    return;
  }
  @autoreleasepool {
    NSEvent *customEvent =
    [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                       location:NSZeroPoint
                  modifierFlags:0
                      timestamp:[NSDate timeIntervalSinceReferenceDate]
                   windowNumber:0
                        context:nil
                        subtype:queue.write
                          data1:msg.wParam
                          data2:0];
    queue.data[queue.write++] = msg;
    [NSApp postEvent:customEvent atStart:NO];
  }
}

void
axPostMessageW(void *obj, uint32_t event, wParam_t wparam, lParam_t lparam)
{
  axPostMessageOwnedW((struct AXmessage){ .target = obj, .message = event,
    .wParam = wparam, .lParam = lparam });
}

void
axNotifyFileDropEvent(char const *filename, float x, float y)
{
  if (!filename || !filename[0]) return;
  char *path = strdup(filename);
  if (!path) return;
  axPostMessageW(NULL, kEventDragDrop,
                 MAKEDWORD((uint16_t)x, (uint16_t)y), path);
}

static uint32_t
modkey(NSEventModifierFlags modifierFlags)
{
  uint32_t flags=0;
  if (modifierFlags & NSEventModifierFlagShift) {
    flags |= AX_MOD_SHIFT;
  }
  if (modifierFlags & NSEventModifierFlagCommand) {
    flags |= AX_MOD_CMD;
  }
  if (modifierFlags & NSEventModifierFlagControl) {
    flags |= AX_MOD_CTRL;
  }
  if (modifierFlags & NSEventModifierFlagOption) {
    flags |= AX_MOD_ALT;
  }
  return flags;
}

static int16_t
normalize_scroll_delta(NSEvent *event, double delta)
{
  /* Precise wheel devices can emit tiny fractions that get lost once the rest
   * of the pipeline converts scroll input into integer steps. */
  if (event.hasPreciseScrollingDeltas && fabs(delta) < 1.0) {
    delta *= 16.0;
  }

  int16_t scaled = (int16_t)lround(delta);
  if (scaled == 0 && delta != 0.0) {
    return delta > 0.0 ? 1 : -1;
  }
  return scaled;
}

static uint32_t
GetEventType(NSEvent *event)
{
  switch (event.type)
  {
    case NSEventTypeLeftMouseDown:
      return event.clickCount == 2 ? kEventLeftDoubleClick : kEventLeftButtonDown;
    case NSEventTypeRightMouseDown:
      return event.clickCount == 2 ? kEventRightDoubleClick : kEventRightButtonDown;
    case NSEventTypeOtherMouseDown:
      return event.clickCount == 2 ? kEventOtherDoubleClick : kEventOtherButtonDown;
    case NSEventTypeLeftMouseUp: return kEventLeftButtonUp;
    case NSEventTypeRightMouseUp: return kEventRightButtonUp;
    case NSEventTypeOtherMouseUp: return kEventOtherButtonUp;
    case NSEventTypeLeftMouseDragged: return kEventLeftButtonDragged;
    case NSEventTypeRightMouseDragged: return kEventRightButtonDragged;
    case NSEventTypeOtherMouseDragged: return kEventOtherButtonDragged;
    case NSEventTypeMouseMoved: return kEventMouseMoved;
    case NSEventTypeScrollWheel: return kEventScrollWheel;
    case NSEventTypeKeyDown: return kEventKeyDown;
    case NSEventTypeKeyUp: return kEventKeyUp;
    case NSEventTypeFlagsChanged: return kEventModifiersChanged;
    case NSEventTypeApplicationDefined:
      return queue.data[(uint16_t)event.subtype].message;
      // case NSEventTypeMouseEntered:
      // case NSEventTypeMouseExited:
      // case NSEventTypeAppKitDefined:
      // case NSEventTypeSystemDefined:
      // case NSEventTypeApplicationDefined:
      // case NSEventTypePeriodic:
      // case NSEventTypeCursorUpdate:
      // case NSEventTypeTabletPoint:
      // case NSEventTypeTabletProximity:
    default:
      return 0;
  }
}

static int
GetKeyCode(NSEvent *event)
{
  switch ([event type])
  {
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
      return [event keyCode];
    default:
      return -1;
  }
}

static int
peek_message(struct AXmessage *e)
{
  NSEvent *event;
  
start_over:
  event = [NSApp nextEventMatchingMask:NSEventMaskAny
                             untilDate:[NSDate date]
                                inMode:NSDefaultRunLoopMode
                               dequeue:YES];
  
  if (!event) {
    /* Headless tests may have posted AX messages queued in queue.data
     without a corresponding Cocoa event available right now.
     Drain that queue directly so rebuild() coroutines can resume. */
    while (queue.read != queue.write && !queue.data[queue.read].message) {
      queue.read++;
    }
    if (queue.read != queue.write) {
      memcpy(e, &queue.data[queue.read], sizeof(struct AXmessage));
      memset(&queue.data[queue.read], 0, sizeof(struct AXmessage));
      queue.read++;
      return 1;
    }
    return 0;
  }
  
  if (event.type == NSEventTypeApplicationDefined) {
    queue.read = event.subtype;
    memcpy(e, &queue.data[queue.read], sizeof(struct AXmessage));
    memset(&queue.data[queue.read], 0, sizeof(struct AXmessage));
    queue.read++;
    if (!e->message) {
      goto start_over;
    }
    return 1;
  } else if (event.type != NSEventTypeKeyDown) {
    [NSApp sendEvent:event];
    [NSApp updateWindows];
  }
  
  if (!GetEventType(event)) {
    goto start_over;
  }
  
  int x = event.locationInWindow.x;
  int y = event.window.contentView.frame.size.height - event.locationInWindow.y;

  memset(e, 0, sizeof(*e));
  e->target = (void *)event.window;
  e->message = GetEventType(event);
  e->wParam = MAKEDWORD(x, y);
  
  /* For double-click events, queue the DoubleClick notification and return
   * MouseDown for this call so that handlers which only listen for MouseDown
   * still process the second click (matches SDL and WinAPI behaviour). */
  uint32_t dbl_followup = 0;
  if      (e->message == kEventLeftDoubleClick)  { dbl_followup = kEventLeftDoubleClick;  e->message = kEventLeftButtonDown;  }
  else if (e->message == kEventRightDoubleClick) { dbl_followup = kEventRightDoubleClick; e->message = kEventRightButtonDown; }
  else if (e->message == kEventOtherDoubleClick) { dbl_followup = kEventOtherDoubleClick; e->message = kEventOtherButtonDown; }
  if (dbl_followup)
    axPostMessageW(e->target, dbl_followup, e->wParam, NULL);
  
  switch([event type]) {
    case NSEventTypeScrollWheel:
      e->lParam = (void*)(intptr_t)MAKEDWORD(normalize_scroll_delta(event, event.scrollingDeltaX),
                                             normalize_scroll_delta(event, event.scrollingDeltaY));
      break;
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
      e->lParam = (void*)(intptr_t)MAKEDWORD((int)event.deltaX, (int)event.deltaY);
      break;
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
      e->wParam = KEY_GetKeyName(GetKeyCode(event)) | modkey(event.modifierFlags);
      strncpy((char *)&e->lParam, event.characters.UTF8String, sizeof(e->lParam));
      break;
    case NSEventTypeFlagsChanged:
      e->wParam = modkey(event.modifierFlags);
      break;
    default:
      break;
  }
  
  return 1;
}

int
axPeekMessage(struct AXmessage *e)
{
  /* nextEventMatchingMask returns a borrowed event, including during native
   * fullscreen transitions. Let the pool release it after decoding. */
  @autoreleasepool {
    return peek_message(e);
  }
}

int
axWaitMessage(longTime_t msec)
{
  @autoreleasepool {
    NSDate *date = msec > 0
    ? [NSDate dateWithTimeIntervalSinceNow:(double)msec / 1000.0]
    : [NSDate distantFuture];
    for (;;) {
      NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:date
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if (!event)
        return 0;
      /* ApplicationDefined events carry AXmessage payloads — put it back so
       axPeekMessage can dequeue and decode it. */
      if (event.type == NSEventTypeApplicationDefined) {
        [NSApp postEvent:event atStart:YES];
        return 1;
      }
      /* Other meaningful events (mouse, key, window) — put back for axPeekMessage. */
      if (GetEventType(event)) {
        [NSApp postEvent:event atStart:YES];
        return 1;
      }
      /* Internal Cocoa events (NSEventTypePeriodic, etc.) — discard and keep waiting. */
      [NSApp sendEvent:event];
    }
  }
}

int
axGetMessage(struct AXmessage *e)
{
  if (axPeekMessage(e)) {
    return 1;
  }

  for (;;) {
    if (axWaitMessage(0) <= 0) {
      return 0;
    }
    if (axPeekMessage(e)) {
      return 1;
    }
  }
}

uint32_t
axSetTimer(void* obj, uint32_t interval_ms, void* userdata, bool_t repeat)
{
  int slot = -1;
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id == 0) { slot = i; break; }
  if (slot < 0)
    return 0;
  uint32_t tid = s_next_timer_id++;
  s_timers[slot].id       = tid;
  s_timers[slot].obj      = obj;
  s_timers[slot].userdata = userdata;
  s_timers[slot].timer = [NSTimer scheduledTimerWithTimeInterval:(double)interval_ms / 1000.0
                                                         repeats:(BOOL)repeat
                                                           block:^(NSTimer* __unused t) {
    axPostMessageW(obj, kEventTimer, tid, userdata);
    if (!repeat && s_timers[slot].id == tid) {
      s_timers[slot].id       = 0;
      s_timers[slot].timer    = nil;
      s_timers[slot].obj      = NULL;
      s_timers[slot].userdata = NULL;
    }
  }];
  return tid;
}

void
axCancelTimer(uint32_t timer_id)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == timer_id) {
      [s_timers[i].timer invalidate];
      s_timers[i].id       = 0;
      s_timers[i].timer    = nil;
      s_timers[i].obj      = NULL;
      s_timers[i].userdata = NULL;
      return;
    }
  }
}
