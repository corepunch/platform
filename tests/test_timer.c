/*
 * test_timer.c – behavioral tests for platform timing and wait functions.
 *
 * Tests axGetMilliseconds, axSleep, and axWaitMessage without requiring a
 * live display server or windowing context.
 */

#include "platform.h"
#include <assert.h>
#include <stdio.h>

/* -------------------------------------------------------------------
 * test_get_milliseconds_non_negative
 *   axGetMilliseconds must return a non-negative monotonic timestamp.
 * ------------------------------------------------------------------- */
static void test_get_milliseconds_non_negative(void)
{
  longTime_t t = axGetMilliseconds();
  /* longTime_t is unsigned, but confirm the call completes without error */
  (void)t;
}

/* -------------------------------------------------------------------
 * test_get_milliseconds_monotonic
 *   Two consecutive calls must return non-decreasing values.
 * ------------------------------------------------------------------- */
static void test_get_milliseconds_monotonic(void)
{
  longTime_t t1 = axGetMilliseconds();
  longTime_t t2 = axGetMilliseconds();
  assert(t2 >= t1);
}

/* -------------------------------------------------------------------
 * test_sleep_accuracy
 *   axSleep(50) must delay for at least 40 ms.
 *   A generous lower bound is used to accommodate slow CI runners.
 * ------------------------------------------------------------------- */
static void test_sleep_accuracy(void)
{
  const longTime_t sleep_ms = 50;
  const longTime_t tolerance = 10; /* allow 10 ms under-shoot */

  longTime_t t1 = axGetMilliseconds();
  axSleep(sleep_ms);
  longTime_t t2 = axGetMilliseconds();

  longTime_t elapsed = t2 - t1;
  assert(elapsed >= sleep_ms - tolerance);
}

/* -------------------------------------------------------------------
 * test_wait_message_timeout
 *   axWaitMessage(timeout) with no display connected must return within a
 *   reasonable wall-clock time and must not crash.
 *
 *   Return value:
 *     0 – timeout (expected on all headless platforms)
 *     1 – an event became ready (e.g. a spurious Win32 message); also OK
 * ------------------------------------------------------------------- */
static void test_wait_message_timeout(void)
{
  const longTime_t timeout_ms = 50;
  /* Allow 5× the requested timeout to account for CI scheduling jitter */
  const longTime_t max_elapsed = timeout_ms * 5;

  longTime_t t1  = axGetMilliseconds();
  int        ret = axWaitMessage(timeout_ms);
  longTime_t t2  = axGetMilliseconds();

  longTime_t elapsed = t2 - t1;

  assert(ret == 0 || ret == 1); /* valid return values */
  assert(elapsed < max_elapsed); /* must not hang indefinitely */
}

int main(void)
{
  test_get_milliseconds_non_negative();
  test_get_milliseconds_monotonic();
  test_sleep_accuracy();
  test_wait_message_timeout();

  printf("All timer tests passed.\n");
  return 0;
}
