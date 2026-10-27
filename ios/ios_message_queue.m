#include "ios_message_queue.h"
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct ios_message {
  struct AXmessage value;
  struct ios_message *next;
} ios_message_t;
static ios_message_t *ios_head, *ios_tail;
static pthread_mutex_t ios_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t ios_next_message = 1, ios_frame_last_message;
static uint32_t ios_paint_message;
static int ios_frame_bounded;

static int ios_message_ready(void) {
  // Unsigned subtraction preserves ordering when the sequence wraps.
  return ios_head && (!ios_frame_bounded ||
      (uint32_t)(ios_frame_last_message - ios_head->value.id) < UINT32_C(0x80000000));
}

void ios_enqueue(struct AXmessage value) {
  pthread_mutex_lock(&ios_queue_mutex);
  if (value.message == kEventWindowPaint || value.message == kEventWindowResized ||
      (ios_paint_message && value.message == ios_paint_message)) {
    for (ios_message_t *p = ios_head; p; p = p->next) {
      if (p->value.target == value.target && p->value.message == value.message) {
        p->value.wParam = value.wParam;
        p->value.lParam = value.lParam;
        pthread_mutex_unlock(&ios_queue_mutex);
        return;
      }
    }
  }
  ios_message_t *p = calloc(1, sizeof(*p));
  if (!p) {
    pthread_mutex_unlock(&ios_queue_mutex);
    fprintf(stderr, "[ios] queue allocation failed target=%p event=%u\n", value.target, value.message);
    return;
  }
  value.id = ios_next_message++;
  p->value = value;
  if (ios_tail) ios_tail->next = p; else ios_head = p;
  ios_tail = p;
  pthread_mutex_unlock(&ios_queue_mutex);
  CFRunLoopWakeUp(CFRunLoopGetMain());
}

void axPostMessageW(void *target, uint32_t event, uint32_t wparam, void *lparam) {
  ios_enqueue((struct AXmessage){ .target = target, .message = event, .wParam = wparam, .lParam = lparam });
}

void ios_set_paint_message(uint32_t message) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_paint_message = message;
  pthread_mutex_unlock(&ios_queue_mutex);
}

void ios_post_display_paint(uint32_t size) {
  pthread_mutex_lock(&ios_queue_mutex);
  int pending = 0;
  for (ios_message_t *p = ios_head; p; p = p->next) {
    if (p->value.message == kEventWindowPaint ||
        (ios_paint_message && p->value.message == ios_paint_message)) {
      pending = 1;
      break;
    }
  }
  pthread_mutex_unlock(&ios_queue_mutex);
  // This runtime has one drawable; a queued application paint already redraws it.
  if (!pending) axPostMessageW(NULL, kEventWindowPaint, size, NULL);
}

void ios_begin_message_frame(void) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_frame_last_message = ios_next_message - 1;
  ios_frame_bounded = 1;
  pthread_mutex_unlock(&ios_queue_mutex);
}

int ios_messages_ready(void) {
  pthread_mutex_lock(&ios_queue_mutex);
  int ready = ios_message_ready();
  pthread_mutex_unlock(&ios_queue_mutex);
  return ready;
}

int axPeekMessage(struct AXmessage *msg) {
  if (!msg) return 0;
  pthread_mutex_lock(&ios_queue_mutex);
  int ready = ios_message_ready();
  if (ready) {
    ios_message_t *p = ios_head;
    *msg = p->value;
    ios_head = p->next;
    if (!ios_head) ios_tail = NULL;
    free(p);
  }
  pthread_mutex_unlock(&ios_queue_mutex);
  return ready;
}

void ios_remove_messages(void *target, uint32_t message, uint32_t wparam) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_message_t **link = &ios_head;
  ios_tail = NULL;
  while (*link) {
    ios_message_t *p = *link;
    if (p->value.target == target && (!message || p->value.message == message) &&
        (wparam == AX_ANY_WPARAM || p->value.wParam == wparam)) {
      *link = p->next;
      if (p->value.message == kEventDragDrop) free(p->value.lParam);
      free(p);
    }
    else { ios_tail = p; link = &p->next; }
  }
  pthread_mutex_unlock(&ios_queue_mutex);
}

void axRemoveMessages(void *target, uint32_t message, uint32_t wparam) {
  if (message) ios_remove_messages(target, message, wparam);
}

void ios_shutdown_messages(void) {
  pthread_mutex_lock(&ios_queue_mutex);
  while (ios_head) {
    ios_message_t *p = ios_head;
    ios_head = p->next;
    if (p->value.message == kEventDragDrop) free(p->value.lParam);
    free(p);
  }
  ios_tail = NULL;
  ios_next_message = 1;
  ios_frame_last_message = 0;
  ios_frame_bounded = 0;
  ios_paint_message = 0;
  pthread_mutex_unlock(&ios_queue_mutex);
}

#ifdef IOS_MESSAGE_QUEUE_TESTING
void ios_test_message_sequence(uint32_t next) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_next_message = next;
  pthread_mutex_unlock(&ios_queue_mutex);
}
#endif
