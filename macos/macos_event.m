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
axRemoveFromQueue(void* target)
{
  for (uint16_t r = queue.read; r != queue.write; r++)
    if (queue.data[r].target == target)
      memset(&queue.data[r], 0, sizeof(queue.data[r]));
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id != 0 && s_timers[i].obj == target)
      axCancelTimer(s_timers[i].id);
}

void
axPostMessageW(void* obj, uint32_t Msg, wParam_t wParam, lParam_t lParam)
{
  struct AXmessage const msg = {
    .target = obj,
    .message = Msg,
    .wParam = wParam,
    .lParam = lParam
  };
  NSEvent *customEvent =
  [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                     location:NSZeroPoint
                modifierFlags:0
                    timestamp:[NSDate timeIntervalSinceReferenceDate]
                 windowNumber:0
                      context:nil
                      subtype:queue.write
                        data1:wParam
                        data2:0];
  for (uint16_t r = queue.read; ++r != queue.write;) {
    if (queue.data[r].message != Msg)
      continue;
    switch (Msg) {
      case kEventWindowResized:
      case kEventWindowPaint:
        queue.data[r] = msg;
        return;
    }
  }
  queue.data[queue.write++] = msg;
  // Post the event to the application's event queue
  [NSApp postEvent:customEvent atStart:NO];
}

void
axNotifyFileDropEvent(char const *filename, float x, float y)
{
//	struct AXmessage * ev       = malloc(sizeof(EVENT));
//	ev->type               = ;
//	ev->windowNumber       = (int)windowNumber;
//	ev->next               = window_events;
//	ev->location.x = x;
//	ev->location.y = y;
//	strcpy(ev->filename, filename);
//	window_events = ev;
  (void)filename;
  (void)x;
  (void)y;
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

static uint32_t
GetEventType(NSEvent *event)
{
	switch (event.type)
	{
    case NSEventTypeLeftMouseDown:
      return event.clickCount == 2 ? kEventLeftDoubleClick : kEventLeftMouseDown;
    case NSEventTypeRightMouseDown:
      return event.clickCount == 2 ? kEventRightDoubleClick : kEventRightMouseDown;
    case NSEventTypeOtherMouseDown:
      return event.clickCount == 2 ? kEventOtherDoubleClick : kEventOtherMouseDown;
    case NSEventTypeLeftMouseUp: return kEventLeftMouseUp;
    case NSEventTypeRightMouseUp: return kEventRightMouseUp;
    case NSEventTypeOtherMouseUp: return kEventOtherMouseUp;
    case NSEventTypeLeftMouseDragged: return kEventLeftMouseDragged;
    case NSEventTypeRightMouseDragged: return kEventRightMouseDragged;
    case NSEventTypeOtherMouseDragged: return kEventOtherMouseDragged;
    case NSEventTypeMouseMoved: return kEventMouseMoved;
    case NSEventTypeScrollWheel: return kEventScrollWheel;
    case NSEventTypeKeyDown: return kEventKeyDown;
    case NSEventTypeKeyUp: return kEventKeyUp;
    case NSEventTypeApplicationDefined:
      return queue.data[(uint16_t)event.subtype].message;
		// case NSEventTypeMouseEntered:
		// case NSEventTypeMouseExited:
		// case NSEventTypeFlagsChanged:
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

int
axPollEvent(struct AXmessage * e)
{
  NSEvent *event;

start_over:
  event = [NSApp nextEventMatchingMask:NSEventMaskAny
                             untilDate:[NSDate date]
                                inMode:NSDefaultRunLoopMode
                               dequeue:YES];

  if (!event)
    return 0;

  if (event.type == NSEventTypeApplicationDefined) {
    queue.read = event.subtype;
    memcpy(e, &queue.data[queue.read], sizeof(struct AXmessage));
    [event release];
    if (!e->message) {
      goto start_over;
    }
    return 1;
  } else if (event.type != NSEventTypeKeyDown) {
    [NSApp sendEvent:event];
    [NSApp updateWindows];
  }
  
  if (!GetEventType(event)) {
    [event release];
    goto start_over;
  }
  
	int x = event.locationInWindow.x;
	int y = event.window.contentView.frame.size.height - event.locationInWindow.y;

	e->target = (void *)event.window;
	e->message = GetEventType(event);
	e->wParam = MAKEDWORD(x, y);

	/* For double-click events, queue the DoubleClick notification and return
	 * MouseDown for this call so that handlers which only listen for MouseDown
	 * still process the second click (matches SDL and WinAPI behaviour). */
	uint32_t dbl_followup = 0;
	if      (e->message == kEventLeftDoubleClick)  { dbl_followup = kEventLeftDoubleClick;  e->message = kEventLeftMouseDown;  }
	else if (e->message == kEventRightDoubleClick) { dbl_followup = kEventRightDoubleClick; e->message = kEventRightMouseDown; }
	else if (e->message == kEventOtherDoubleClick) { dbl_followup = kEventOtherDoubleClick; e->message = kEventOtherMouseDown; }
	if (dbl_followup)
	  axPostMessageW(e->target, dbl_followup, e->wParam, NULL);
	
	switch([event type]) {
	case NSEventTypeScrollWheel:
		e->lParam = (void*)(intptr_t)MAKEDWORD((int)event.scrollingDeltaX,
                                           (int)event.scrollingDeltaY);
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
	default:
		break;
	}
	
	[event release];
	
	return 1;
}

int
axWaitEvent(longTime_t msec)
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
         axPollEvent can dequeue and decode it. */
      if (event.type == NSEventTypeApplicationDefined) {
        [NSApp postEvent:event atStart:YES];
        return 1;
      }
      /* Other meaningful events (mouse, key, window) — put back for axPollEvent. */
      if (GetEventType(event)) {
        [NSApp postEvent:event atStart:YES];
        return 1;
      }
      /* Internal Cocoa events (NSEventTypePeriodic, etc.) — discard and keep waiting. */
      [NSApp sendEvent:event];
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
