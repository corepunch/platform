#import <AppKit/AppKit.h>
#include "platform.h"
#include <assert.h>
#include <stdio.h>

static void drain_events(void)
{
  struct AXmessage msg;
  while (axPeekMessage(&msg)) {}
}

static void test_event_lifetime(void)
{
  for (int i = 0; i < 128; i++) {
    @autoreleasepool {
      struct AXmessage msg;
      void *target = (void *)&test_event_lifetime;
      axPostMessageW(target, kEventKeyDown, i, NULL);
      assert(axWaitMessage(100) == 1);
      assert(axPeekMessage(&msg) == 1);
      assert(msg.target == target && msg.wParam == i);
      assert(msg.message == kEventKeyDown);

      NSEvent *key = [NSEvent keyEventWithType:NSEventTypeKeyDown
        location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0
        context:nil characters:@"a" charactersIgnoringModifiers:@"a"
        isARepeat:NO keyCode:0];
      [NSApp postEvent:key atStart:NO];
      assert(axPeekMessage(&msg) == 1);
      assert(msg.message == kEventKeyDown);
      assert(((char *)&msg.lParam)[0] == 'a');

      NSEvent *ignored = [NSEvent otherEventWithType:NSEventTypePeriodic
        location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0
        context:nil subtype:0 data1:0 data2:0];
      [NSApp postEvent:ignored atStart:NO];
      drain_events();
    }
  }
}

// Removed messages leave cleared slots that Cocoa still points at.
static void test_remove_messages(void)
{
  struct AXmessage msg;
  void *a = (void *)&test_remove_messages, *b = (void *)&drain_events;
  drain_events();
  axPostMessageW(a, kEventTimer, 1, NULL);
  axPostMessageW(a, kEventTimer, 2, NULL);
  axPostMessageW(b, kEventTimer, 1, NULL);
  axPostMessageW(a, kEventKeyDown, 3, NULL);
  axPostMessageW(a, kEventTimer, 1, NULL);
  axRemoveMessages(a, kEventTimer, 1);
  assert(axPeekMessage(&msg) == 1 && msg.target == a && msg.message == kEventTimer && msg.wParam == 2);
  assert(axPeekMessage(&msg) == 1 && msg.target == b && msg.message == kEventTimer && msg.wParam == 1);
  assert(axPeekMessage(&msg) == 1 && msg.target == a && msg.message == kEventKeyDown);
  axPostMessageW(a, kEventTimer, 4, NULL);
  axPostMessageW(a, kEventKeyUp, 5, NULL);
  axRemoveMessages(a, kEventTimer, AX_ANY_WPARAM);
  assert(axPeekMessage(&msg) == 1 && msg.message == kEventKeyUp && msg.wParam == 5);
  drain_events();
}

static void discard_wake_events(void)
{
  while ([NSApp nextEventMatchingMask:NSEventMaskApplicationDefined
                            untilDate:[NSDate date]
                               inMode:NSDefaultRunLoopMode
                              dequeue:YES]) {}
}

static void test_resize_without_wake_events(void)
{
  struct AXmessage msg;
  wParam_t initial_size = MAKEDWORD(640, 480);
  wParam_t final_size = MAKEDWORD(800, 600);
  drain_events();
  axPostMessageW(NULL, kEventWindowResized, initial_size, NULL);
  axPostMessageW(NULL, kEventWindowPaint, initial_size, NULL);
  // Native event tracking can consume wake events before ORCA polls them.
  discard_wake_events();
  axPostMessageW(NULL, kEventWindowResized, final_size, NULL);
  axPostMessageW(NULL, kEventWindowPaint, final_size, NULL);
  assert(axWaitMessage(100) == 1);
  assert(axPeekMessage(&msg) && msg.message == kEventWindowResized && msg.wParam == final_size);

  // A later wake must still deliver the coalesced paint first.
  axPostMessageW(NULL, kEventTimer, 0, NULL);
  assert(axPeekMessage(&msg) && msg.message == kEventWindowPaint && msg.wParam == final_size);
  assert(axPeekMessage(&msg) && msg.message == kEventTimer);
  assert(!axPeekMessage(&msg));
}

static void test_fullscreen(void)
{
  assert(axCreateWindow("Fullscreen event test", 640, 480,
    AX_WINDOW_RESIZABLE | AX_WINDOW_DOUBLEBUFFER));
  NSWindow *window = nil;
  for (NSWindow *candidate in [NSApp windows])
    if ([candidate.title isEqualToString:@"Fullscreen event test"]) window = candidate;
  assert(window);
  for (int i = 0; i < 4; i++) {
    BOOL fullscreen = !(window.styleMask & NSWindowStyleMaskFullScreen);
    [window toggleFullScreen:nil];
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:3];
    while ([deadline timeIntervalSinceNow] > 0) {
      @autoreleasepool {
        drain_events();
        axWaitMessage(10);
      }
    }
    assert(!!(window.styleMask & NSWindowStyleMaskFullScreen) == fullscreen);
  }
}

int main(int argc, char **argv)
{
  @autoreleasepool {
    axInit();
    drain_events();
    test_event_lifetime();
    test_remove_messages();
    test_resize_without_wake_events();
    if (argc > 1 && !strcmp(argv[1], "--fullscreen")) test_fullscreen();
    puts("macOS event lifetime tests passed.");
  }
  return 0;
}
