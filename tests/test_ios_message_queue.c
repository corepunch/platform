#include "ios/ios_message_queue.h"
#include <assert.h>

// Test-only sequence seed makes wraparound coverage independent of queue size.
extern void ios_test_message_sequence(uint32_t next);

static void test_unbounded_and_coalesced(void) {
  ios_shutdown_messages();
  struct AXmessage msg;
  assert(!axPeekMessage(NULL) && !ios_messages_ready());
  axPostMessageW(NULL, kEventWindowPaint, 1, NULL);
  axPostMessageW(NULL, kEventWindowPaint, 2, NULL);
  axPostMessageW(NULL, kEventKeyDown, 3, NULL);
  assert(ios_messages_ready());
  assert(axPeekMessage(&msg) && msg.message == kEventWindowPaint && msg.wParam == 2);
  axPostMessageW(NULL, kEventKeyUp, 4, NULL);
  assert(axPeekMessage(&msg) && msg.message == kEventKeyDown && msg.wParam == 3);
  assert(axPeekMessage(&msg) && msg.message == kEventKeyUp && msg.wParam == 4);
  assert(!axPeekMessage(&msg));
}

static void test_inertia_interrupt(void) {
  ios_shutdown_messages();
  struct AXmessage msg;
  axPostMessageW(NULL, kEventWindowPaint, 0, NULL);
  ios_begin_message_frame();
  int paints = 0;
  while (axPeekMessage(&msg)) {
    assert(msg.message == kEventWindowPaint && ++paints == 1);
    axPostMessageW(NULL, kEventWindowPaint, 0, NULL);
  }
  assert(paints == 1 && !ios_messages_ready());

  // UIKit can now deliver a new touch while another inertia paint is pending.
  ios_enqueue((struct AXmessage){ .message = kEventScrollWheel,
      .pointer = { .flags = AX_POINTER_TOUCH | AX_POINTER_SCROLL_BEGIN } });
  assert(!axPeekMessage(&msg));
  ios_begin_message_frame();
  assert(axPeekMessage(&msg) && msg.message == kEventWindowPaint);
  axPostMessageW(NULL, kEventWindowPaint, 0, NULL);
  assert(axPeekMessage(&msg) && msg.message == kEventScrollWheel);
  assert(msg.pointer.flags == (AX_POINTER_TOUCH | AX_POINTER_SCROLL_BEGIN));
  assert(!axPeekMessage(&msg) && !ios_messages_ready());

  ios_begin_message_frame();
  assert(axPeekMessage(&msg) && msg.message == kEventWindowPaint);
  assert(!axPeekMessage(&msg));
}

static void test_complete_snapshot(void) {
  ios_shutdown_messages();
  struct AXmessage msg;
  ios_begin_message_frame();
  for (uint32_t i = 0; i < 512; i++) axPostMessageW(NULL, kEventMouseMoved, i, NULL);
  assert(!axPeekMessage(&msg));
  ios_begin_message_frame();
  for (uint32_t i = 0; i < 512; i++) {
    assert(axPeekMessage(&msg) && msg.wParam == i);
    axPostMessageW(NULL, kEventMouseMoved, i + 512, NULL);
  }
  assert(!axPeekMessage(&msg));
  ios_begin_message_frame();
  for (uint32_t i = 512; i < 1024; i++) assert(axPeekMessage(&msg) && msg.wParam == i);
  assert(!axPeekMessage(&msg));
}

static void test_removal_and_reset(void) {
  ios_shutdown_messages();
  int first, second;
  struct AXmessage msg;
  axPostMessageW(&first, kEventKeyDown, 1, NULL);
  axPostMessageW(&second, kEventKeyDown, 2, NULL);
  axPostMessageW(&first, kEventDragDrop, 0, strdup("removed file"));
  ios_begin_message_frame();
  axPostMessageW(&second, kEventKeyUp, 3, NULL);
  ios_remove_messages(&first, 0, AX_ANY_WPARAM);
  assert(axPeekMessage(&msg) && msg.target == &second && msg.wParam == 2);
  assert(!axPeekMessage(&msg));
  ios_remove_messages(&second, 0, AX_ANY_WPARAM);
  axPostMessageW(&first, kEventKeyDown, 4, NULL);
  assert(!axPeekMessage(&msg));
  ios_begin_message_frame();
  assert(axPeekMessage(&msg) && msg.wParam == 4);
  axPostMessageW(&first, kEventDragDrop, 0, strdup("shutdown file"));
  ios_shutdown_messages();
  assert(!axPeekMessage(&msg));
  axPostMessageW(&second, kEventKeyDown, 5, NULL);
  assert(axPeekMessage(&msg) && msg.id == 1 && msg.wParam == 5);
  assert(!axPeekMessage(&msg));
}

static void test_remove_by_type(void) {
  ios_shutdown_messages();
  int first, second;
  struct AXmessage msg;
  axPostMessageW(&first, kEventTimer, 1, NULL);
  axPostMessageW(&first, kEventTimer, 2, NULL);
  axPostMessageW(&second, kEventTimer, 1, NULL);
  axPostMessageW(&first, kEventKeyDown, 3, NULL);
  axRemoveMessages(&first, kEventTimer, 1);
  assert(axPeekMessage(&msg) && msg.target == &first && msg.message == kEventTimer && msg.wParam == 2);
  assert(axPeekMessage(&msg) && msg.target == &second && msg.wParam == 1);
  axPostMessageW(&first, kEventTimer, 4, NULL);
  axRemoveMessages(&first, kEventTimer, AX_ANY_WPARAM);
  assert(axPeekMessage(&msg) && msg.message == kEventKeyDown && msg.wParam == 3);
  assert(!axPeekMessage(&msg));
}

static void test_sequence_wrap(void) {
  ios_shutdown_messages();
  ios_test_message_sequence(UINT32_MAX - 1);
  struct AXmessage msg;
  axPostMessageW(NULL, kEventKeyDown, 1, NULL);
  axPostMessageW(NULL, kEventKeyDown, 2, NULL);
  ios_begin_message_frame();
  axPostMessageW(NULL, kEventKeyDown, 3, NULL);
  axPostMessageW(NULL, kEventKeyDown, 4, NULL);
  assert(axPeekMessage(&msg) && msg.id == UINT32_MAX - 1 && msg.wParam == 1);
  assert(axPeekMessage(&msg) && msg.id == UINT32_MAX && msg.wParam == 2);
  assert(!axPeekMessage(&msg));
  ios_begin_message_frame();
  axPostMessageW(NULL, kEventKeyDown, 5, NULL);
  assert(axPeekMessage(&msg) && msg.id == 0 && msg.wParam == 3);
  assert(axPeekMessage(&msg) && msg.id == 1 && msg.wParam == 4);
  assert(!axPeekMessage(&msg));
  ios_begin_message_frame();
  assert(axPeekMessage(&msg) && msg.id == 2 && msg.wParam == 5);
  assert(!axPeekMessage(&msg));
}

int main(void) {
  test_unbounded_and_coalesced();
  test_inertia_interrupt();
  test_complete_snapshot();
  test_removal_and_reset();
  test_remove_by_type();
  test_sequence_wrap();
  ios_shutdown_messages();
  puts("PASS: iOS queue frame boundaries, touch interruption, FIFO, removal by target and type, reset and sequence wrap");
  return 0;
}
