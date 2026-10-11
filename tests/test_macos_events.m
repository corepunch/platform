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
    if (argc > 1 && !strcmp(argv[1], "--fullscreen")) test_fullscreen();
    puts("macOS event lifetime tests passed.");
  }
  return 0;
}
