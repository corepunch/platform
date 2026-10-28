/*
 * test_messages.c – behavioral tests for the platform event-queue API.
 *
 * Tests axPostMessageW, axPeekMessage, axRemoveFromQueue and
 * axRemoveMessages without
 * requiring a live display server or windowing context.
 *
 * On macOS axInit initializes NSApp without opening a render window so the
 * same delivery and ownership checks exercise the Cocoa-backed queue.
 */

#include "platform.h"
#include <assert.h>
#include <stdio.h>

/* -------------------------------------------------------------------
 * Helper: drain any leftover events from previous sub-tests.
 * ------------------------------------------------------------------- */
static void drain_queue(void)
{
  struct AXmessage msg;
  while (axPeekMessage(&msg))
    axDisposeMessage(&msg);
}

/* -------------------------------------------------------------------
 * test_empty_queue
 *   axPeekMessage must return 0 on an empty queue.
 * ------------------------------------------------------------------- */
static void test_empty_queue(void)
{
  struct AXmessage msg;
  drain_queue();
  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_post_and_poll
 *   A single posted event must be retrievable with the correct fields.
 * ------------------------------------------------------------------- */
static void test_post_and_poll(void)
{
  struct AXmessage msg;
  void *handle = (void *)0xDEAD;

  drain_queue();

  axPostMessageW(handle, kEventWindowPaint, 0x1234, NULL);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.target  == handle);
  assert(msg.message == kEventWindowPaint);
  assert(msg.wParam  == 0x1234);
  assert(msg.lParam  == NULL);

  /* Queue must be empty afterwards */
  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_fifo_ordering
 *   Events must be delivered in FIFO order.
 * ------------------------------------------------------------------- */
static void test_fifo_ordering(void)
{
  struct AXmessage msg;
  void *handle = (void *)0xBEEF;

  drain_queue();

  axPostMessageW(handle, kEventKeyDown,    1, NULL);
  axPostMessageW(handle, kEventKeyUp,      2, NULL);
  axPostMessageW(handle, kEventMouseMoved, 3, NULL);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.message == kEventKeyDown    && msg.wParam == 1);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.message == kEventKeyUp      && msg.wParam == 2);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.message == kEventMouseMoved && msg.wParam == 3);

  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_remove_from_queue
 *   AX_RemoveFromQueue must remove only events for the specified target.
 * ------------------------------------------------------------------- */
static void test_remove_from_queue(void)
{
  struct AXmessage msg;
  void *handle_a = (void *)0xAAAA;
  void *handle_b = (void *)0xBBBB;

  drain_queue();

  axPostMessageW(handle_a, kEventKeyDown,    10, NULL);
  axPostMessageW(handle_b, kEventKeyDown,    20, NULL);
  axPostMessageW(handle_a, kEventKeyUp,      30, NULL);
  axPostMessageW(handle_b, kEventMouseMoved, 40, NULL);

  /* Remove only handle_a's events */
  axRemoveFromQueue(handle_a);

  /* Only handle_b's events should remain, in original order */
  assert(axPeekMessage(&msg) == 1);
  assert(msg.target == handle_b && msg.wParam == 20);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.target == handle_b && msg.wParam == 40);

  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_remove_nonexistent_target
 *   AX_RemoveFromQueue with an unknown target must leave other events intact.
 * ------------------------------------------------------------------- */
static void test_remove_nonexistent_target(void)
{
  struct AXmessage msg;
  void *handle = (void *)0xCCCC;

  drain_queue();

  axPostMessageW(handle, kEventWindowPaint, 99, NULL);

  /* Removing a different handle must be a no-op */
  axRemoveFromQueue((void *)0xDDDD);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.wParam == 99);

  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_remove_messages
 *   axRemoveMessages must remove only one type for one target, matching
 *   wParam exactly unless AX_ANY_WPARAM is given.
 * ------------------------------------------------------------------- */
static void test_remove_messages(void)
{
  struct AXmessage msg;
  void *handle_a = (void *)0xA0A0;
  void *handle_b = (void *)0xB0B0;

  drain_queue();

  axPostMessageW(handle_a, kEventTimer,   1, NULL);
  axPostMessageW(handle_a, kEventTimer,   2, NULL);
  axPostMessageW(handle_b, kEventTimer,   1, NULL);
  axPostMessageW(handle_a, kEventKeyDown, 1, NULL);
  axPostMessageW(handle_a, kEventTimer,   1, NULL);

  /* One timer of handle_a: other timers, targets and types stay queued */
  axRemoveMessages(handle_a, kEventTimer, 1);
  assert(axPeekMessage(&msg) == 1);
  assert(msg.target == handle_a && msg.message == kEventTimer && msg.wParam == 2);
  assert(axPeekMessage(&msg) == 1);
  assert(msg.target == handle_b && msg.message == kEventTimer && msg.wParam == 1);
  assert(axPeekMessage(&msg) == 1);
  assert(msg.target == handle_a && msg.message == kEventKeyDown);
  assert(axPeekMessage(&msg) == 0);

  /* AX_ANY_WPARAM removes the type whatever its wParam */
  axPostMessageW(handle_a, kEventTimer,   3, NULL);
  axPostMessageW(handle_a, kEventKeyDown, 4, NULL);
  axPostMessageW(handle_a, kEventTimer,   5, NULL);
  axRemoveMessages(handle_a, kEventTimer, AX_ANY_WPARAM);
  assert(axPeekMessage(&msg) == 1);
  assert(msg.message == kEventKeyDown && msg.wParam == 4);
  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_lparam_roundtrip
 *   The lParam pointer must survive the queue round-trip unchanged.
 * ------------------------------------------------------------------- */
static void test_lparam_roundtrip(void)
{
  struct AXmessage msg;
  void *handle  = (void *)0x1111;
  void *payload = (void *)0xCAFEBABE;

  drain_queue();

  axPostMessageW(handle, kEventKeyDown, 0, payload);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.lParam == payload);

  drain_queue();
}

/* -------------------------------------------------------------------
 * test_double_click_sequence
 *   On every platform, a double-click emits kEventLeftButtonDown followed
 *   by kEventLeftDoubleClick for the second click.  Both events must be
 *   delivered in that order so that handlers which only watch for
 *   kEventLeftButtonDown still see the second click (SDL / WinAPI parity).
 * ------------------------------------------------------------------- */
static void test_double_click_sequence(void)
{
  struct AXmessage msg;
  void *handle = (void *)0xF00D;

  drain_queue();

  /* Simulate what the platform backends now emit on the second click of a
   * double-click: MouseDown followed immediately by DoubleClick. */
  axPostMessageW(handle, kEventLeftButtonDown,  0x0064005A, NULL);
  axPostMessageW(handle, kEventLeftDoubleClick, 0x0064005A, NULL);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.message == kEventLeftButtonDown);
  assert(msg.wParam  == 0x0064005A);

  assert(axPeekMessage(&msg) == 1);
  assert(msg.message == kEventLeftDoubleClick);
  assert(msg.wParam  == 0x0064005A);

  assert(axPeekMessage(&msg) == 0);
}

/* -------------------------------------------------------------------
 * test_get_message
 *   axGetMessage should return the next queued event.
 * ------------------------------------------------------------------- */
static void test_get_message(void)
{
  struct AXmessage msg;
  void *handle = (void *)0xABCD;

  drain_queue();

  axPostMessageW(handle, kEventWindowPaint, 0x55AA, NULL);

  assert(axGetMessage(&msg) == 1);
  assert(msg.target  == handle);
  assert(msg.message == kEventWindowPaint);
  assert(msg.wParam  == 0x55AA);

  assert(axPeekMessage(&msg) == 0);
}

static unsigned disposed;
static void dispose_owned(void *value) { disposed++; free(value); }

static void post_owned(void *target, uint32_t event, uint32_t word)
{
  void *value = malloc(8);
  assert(value);
  struct AXmessage message = { .target = target, .message = event, .wParam = word, .lParam = value };
  assert(axOwnMessageData(&message, value, dispose_owned));
  axPostMessageOwnedW(message);
}

static void test_owned_coalescing(void)
{
  struct AXmessage message;
  int a, b;
  drain_queue();
  for (unsigned i = 0; i < 2; i++) {
    uint32_t event = i ? kEventWindowResized : kEventWindowPaint;
    unsigned before = disposed;
    post_owned(&a, event, 1);
    post_owned(&b, event, 2);
    post_owned(&a, kEventKeyDown, 3);
    post_owned(&a, event, 4);
    assert(disposed == before + 1);
    assert(axPeekMessage(&message) && message.target == &a && message.wParam == 4);
    axDisposeMessage(&message);
    assert(axPeekMessage(&message) && message.target == &b && message.wParam == 2);
    axDisposeMessage(&message);
    assert(axPeekMessage(&message) && message.message == kEventKeyDown && message.wParam == 3);
    axDisposeMessage(&message);
    assert(!axPeekMessage(&message) && disposed == before + 4);
  }
}

int main(void)
{
#ifdef __APPLE__
  axInit();
#endif
  test_empty_queue();
  test_post_and_poll();
  test_fifo_ordering();
  test_remove_from_queue();
  test_remove_nonexistent_target();
  test_remove_messages();
  test_lparam_roundtrip();
  test_double_click_sequence();
  test_get_message();
  test_owned_coalescing();

  printf("All message queue tests passed.\n");
  return 0;
}
