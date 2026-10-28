#include "ios_message_queue.h"
#include <pthread.h>
#include <CoreFoundation/CoreFoundation.h>

typedef struct ios_message {
  struct AXmessage value;
  struct ios_message *next;
} ios_message_t;
static ios_message_t *ios_head, *ios_tail;
static pthread_mutex_t ios_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t ios_next_message = 1, ios_frame_last_message, ios_frame_last_paint;
static uint32_t ios_paint_message;
static int ios_frame_bounded, ios_frame_drained;
static unsigned ios_paints_delivered;

static int ios_is_paint(struct AXmessage const *value) {
  return value->message == kEventWindowPaint ||
         (ios_paint_message && value->message == ios_paint_message);
}

// Unsigned subtraction preserves ordering when the sequence wraps.
static int ios_posted_by(uint32_t id, uint32_t last) {
  return (uint32_t)(last - id) < UINT32_C(0x80000000);
}

// Finds the next readable message. Messages posted during a display callback
// wait for the next one, so UIKit can deliver a touch between animation
// steps. The paints they request are the exception: once the callback's
// batch has drained, the paints queued so far draw in the same callback, so
// every animation step and touch is shown in the frame that handled it.
// Paints posted by those paints wait for the next callback.
static int ios_ready_message(ios_message_t **prev_out, ios_message_t **out) {
  ios_message_t *prev = NULL, *p = ios_head;
  if (!p) return 0;
  if (ios_frame_bounded && !ios_posted_by(p->value.id, ios_frame_last_message)) {
    if (!ios_frame_drained) {
      ios_frame_drained = 1;
      ios_frame_last_paint = ios_next_message - 1;
    }
    for (; p && !(ios_is_paint(&p->value) && ios_posted_by(p->value.id, ios_frame_last_paint)); p = p->next)
      prev = p;
    if (!p) return 0;
  }
  *prev_out = prev;
  *out = p;
  return 1;
}

static void ios_unlink(ios_message_t *prev, ios_message_t *p) {
  if (prev) prev->next = p->next; else ios_head = p->next;
  if (ios_tail == p) ios_tail = prev;
  free(p);
}

void ios_enqueue(struct AXmessage value) {
  pthread_mutex_lock(&ios_queue_mutex);
  if (value.message == kEventWindowResized || ios_is_paint(&value)) {
    for (ios_message_t *p = ios_head; p; p = p->next) {
      if (p->value.target == value.target && p->value.message == value.message) {
        struct AXmessage old = p->value;
        value.id = old.id;
        p->value = value;
        pthread_mutex_unlock(&ios_queue_mutex);
        axDisposeMessage(&old);
        return;
      }
    }
  }
  ios_message_t *p = calloc(1, sizeof(*p));
  if (!p) {
    pthread_mutex_unlock(&ios_queue_mutex);
    fprintf(stderr, "[ios] queue allocation failed target=%p event=%u\n", value.target, value.message);
    axDisposeMessage(&value);
    return;
  }
  value.id = ios_next_message++;
  p->value = value;
  if (ios_tail) ios_tail->next = p; else ios_head = p;
  ios_tail = p;
  pthread_mutex_unlock(&ios_queue_mutex);
  CFRunLoopWakeUp(CFRunLoopGetMain());
}

void axPostMessageOwnedW(struct AXmessage message) { ios_enqueue(message); }

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
  for (ios_message_t *p = ios_head; p && !pending; p = p->next) pending = ios_is_paint(&p->value);
  pthread_mutex_unlock(&ios_queue_mutex);
  // This runtime has one drawable; a queued application paint already redraws it.
  if (!pending) axPostMessageW(NULL, kEventWindowPaint, size, NULL);
}

void ios_begin_message_frame(void) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_frame_last_message = ios_next_message - 1;
  ios_frame_bounded = 1;
  ios_frame_drained = 0;
  pthread_mutex_unlock(&ios_queue_mutex);
}

int ios_messages_ready(void) {
  ios_message_t *prev, *p;
  pthread_mutex_lock(&ios_queue_mutex);
  int ready = ios_ready_message(&prev, &p);
  pthread_mutex_unlock(&ios_queue_mutex);
  return ready;
}

int axPeekMessage(struct AXmessage *msg) {
  if (!msg) return 0;
  ios_message_t *prev, *p;
  pthread_mutex_lock(&ios_queue_mutex);
  int ready = ios_ready_message(&prev, &p);
  if (ready) {
    *msg = p->value;
    ios_paints_delivered += ios_is_paint(&p->value);
    ios_unlink(prev, p);
  }
  pthread_mutex_unlock(&ios_queue_mutex);
  return ready;
}

void ios_remove_messages(void *target, uint32_t message, uint32_t wparam) {
  ios_message_t *removed = NULL;
  pthread_mutex_lock(&ios_queue_mutex);
  ios_message_t **link = &ios_head;
  ios_tail = NULL;
  while (*link) {
    ios_message_t *p = *link;
    if (p->value.target == target && (!message || p->value.message == message) &&
        (wparam == AX_ANY_WPARAM || p->value.wParam == wparam)) {
      *link = p->next;
      p->next = removed;
      removed = p;
    }
    else { ios_tail = p; link = &p->next; }
  }
  pthread_mutex_unlock(&ios_queue_mutex);
  while (removed) {
    ios_message_t *next = removed->next;
    if (removed->value.message == kEventDragDrop && !removed->value.ownedPayload) free(removed->value.lParam);
    axDisposeMessage(&removed->value);
    free(removed);
    removed = next;
  }
}

void axRemoveMessages(void *target, uint32_t message, uint32_t wparam) {
  if (message) ios_remove_messages(target, message, wparam);
}

unsigned ios_paint_count(void) { return ios_paints_delivered; }

void ios_shutdown_messages(void) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_message_t *removed = ios_head;
  ios_head = ios_tail = NULL;
  ios_next_message = 1;
  ios_frame_last_message = ios_frame_last_paint = 0;
  ios_frame_bounded = ios_frame_drained = 0;
  ios_paint_message = 0;
  pthread_mutex_unlock(&ios_queue_mutex);
  while (removed) {
    ios_message_t *next = removed->next;
    if (removed->value.message == kEventDragDrop && !removed->value.ownedPayload) free(removed->value.lParam);
    axDisposeMessage(&removed->value);
    free(removed);
    removed = next;
  }
}

void axClearMessageQueue(void) { ios_shutdown_messages(); }

#ifdef IOS_MESSAGE_QUEUE_TESTING
void ios_test_message_sequence(uint32_t next) {
  pthread_mutex_lock(&ios_queue_mutex);
  ios_next_message = next;
  pthread_mutex_unlock(&ios_queue_mutex);
}
#endif
