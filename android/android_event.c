#include "android_local.h"
#include <android/configuration.h>
#include <android/input.h>
#include <android/looper.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/timerfd.h>
#include <unistd.h>

/* Looper identifiers after the glue's LOOPER_ID_MAIN and LOOPER_ID_INPUT. */
enum { ANDROID_LOOPER_INBOX = LOOPER_ID_USER, ANDROID_LOOPER_TIMER };

static struct {
  struct AXmessage queue[0x10000];
  uint16_t write, read;
} events;

#define MAX_TIMERS 64
static struct {
  uint32_t id;
  int fd;
  void *obj, *userdata;
  bool_t repeat;
} s_timers[MAX_TIMERS];
static uint32_t s_next_timer_id = 1;

/* Messages from Java/UI threads wait here; a pipe wakes the looper. */
#define INBOX_CAPACITY 256
static pthread_mutex_t inbox_lock = PTHREAD_MUTEX_INITIALIZER;
static struct AXmessage inbox[INBOX_CAPACITY];
static unsigned inbox_count;
static int inbox_pipe[2] = { -1, -1 };

void
axPostMessageOwnedW(struct AXmessage message)
{
  if (message.message == kEventWindowPaint || message.message == kEventWindowResized) {
    for (uint16_t r = events.read; r != events.write; r++) {
      if (events.queue[r].target != message.target || events.queue[r].message != message.message) continue;
      struct AXmessage old = events.queue[r];
      events.queue[r] = message;
      axDisposeMessage(&old);
      return;
    }
  }
  if ((uint16_t)(events.write + 1) == events.read) { axDisposeMessage(&message); return; }
  events.queue[events.write++] = message;
}

void
axPostMessageW(void *hobj, uint32_t event, uint32_t wparam, void *lparam)
{
  axPostMessageOwnedW((struct AXmessage){ .target = hobj, .message = event, .wParam = wparam, .lParam = lparam });
}

void
axAndroidPostMessage(struct AXmessage message)
{
  pthread_mutex_lock(&inbox_lock);
  bool_t stored = inbox_count < INBOX_CAPACITY;
  if (stored) inbox[inbox_count++] = message;
  pthread_mutex_unlock(&inbox_lock);
  if (!stored) { axDisposeMessage(&message); return; }
  char wake = 1;
  if (inbox_pipe[1] >= 0) (void)!write(inbox_pipe[1], &wake, 1);
}

static void
inbox_drain(void)
{
  char buffer[64];
  if (inbox_pipe[0] >= 0) while (read(inbox_pipe[0], buffer, sizeof(buffer)) > 0) {}
  struct AXmessage pending[INBOX_CAPACITY];
  pthread_mutex_lock(&inbox_lock);
  unsigned count = inbox_count;
  memcpy(pending, inbox, count * sizeof(*inbox));
  inbox_count = 0;
  pthread_mutex_unlock(&inbox_lock);
  for (unsigned i = 0; i < count; i++) {
    if (pending[i].message == kEventWindowResized && !pending[i].wParam) pending[i].wParam = android_point_size();
    axPostMessageOwnedW(pending[i]);
  }
}

static void
timers_poll(void)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (!s_timers[i].id) continue;
    uint64_t expirations = 0;
    if (read(s_timers[i].fd, &expirations, sizeof(expirations)) == (ssize_t)sizeof(expirations) && expirations) {
      axPostMessageOwnedW((struct AXmessage){ .target = s_timers[i].obj, .message = kEventTimer,
        .wParam = s_timers[i].id, .lParam = s_timers[i].userdata });
      if (!s_timers[i].repeat) axCancelTimer(s_timers[i].id);
    }
  }
}

uint32_t
axSetTimer(void *obj, uint32_t interval_ms, void *userdata, bool_t repeat)
{
  int slot = -1;
  for (int i = 0; i < MAX_TIMERS && slot < 0; i++) if (!s_timers[i].id) slot = i;
  if (slot < 0) return 0;
  int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd < 0) return 0;
  if (!interval_ms) interval_ms = 1;
  struct timespec period = { .tv_sec = interval_ms / 1000, .tv_nsec = (long)(interval_ms % 1000) * 1000000L };
  struct itimerspec its = { .it_value = period, .it_interval = repeat ? period : (struct timespec){ 0 } };
  if (timerfd_settime(fd, 0, &its, NULL) < 0) { close(fd); return 0; }
  ALooper *looper = ALooper_forThread();
  if (looper) ALooper_addFd(looper, fd, ANDROID_LOOPER_TIMER, ALOOPER_EVENT_INPUT, NULL, NULL);
  uint32_t tid = s_next_timer_id++;
  if (!s_next_timer_id) s_next_timer_id = 1;
  s_timers[slot].id = tid;
  s_timers[slot].fd = fd;
  s_timers[slot].obj = obj;
  s_timers[slot].userdata = userdata;
  s_timers[slot].repeat = repeat;
  return tid;
}

void
axCancelTimer(uint32_t timer_id)
{
  if (!timer_id) return;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id != timer_id) continue;
    ALooper *looper = ALooper_forThread();
    if (looper) ALooper_removeFd(looper, s_timers[i].fd);
    close(s_timers[i].fd);
    memset(&s_timers[i], 0, sizeof(s_timers[i]));
    return;
  }
}

/* Pumps the looper once: glue commands and input run their callbacks, which
 * post messages; timers and the cross-thread inbox are drained here. */
int
android_pump(int timeout_ms)
{
  if (!android.app) return 0;
  int events_mask, handled = 0, ident;
  struct android_poll_source *source = NULL;
  while ((ident = ALooper_pollOnce(handled ? 0 : timeout_ms, NULL, &events_mask, (void **)&source)) >= 0) {
    handled = 1;
    if (source) source->process(android.app, source);
    else if (ident == ANDROID_LOOPER_INBOX) inbox_drain();
    else if (ident == ANDROID_LOOPER_TIMER) timers_poll();
    source = NULL;
    if (android.app->destroyRequested) break;
  }
  return handled;
}

int
axWaitMessage(longTime_t timeout_ms)
{
  if (events.read != events.write) return 1;
  android_pump(timeout_ms > 0 ? (int)timeout_ms : -1);
  return events.read != events.write;
}

int
axPeekMessage(struct AXmessage *pEvent)
{
  android_pump(0);
  if (events.read == events.write) return 0;
  *pEvent = events.queue[events.read++];
  return 1;
}

int
axGetMessage(struct AXmessage *pEvent)
{
  while (!axPeekMessage(pEvent)) {
    if (android.destroy) return 0;
    axWaitMessage(0);
  }
  return 1;
}

void
NotifyWindowEvent(void *window, uint32_t eventType, uint32_t wparam)
{
  axPostMessageOwnedW((struct AXmessage){ .wParam = wparam, .message = eventType, .target = window });
}

void
axClearMessageQueue(void)
{
  struct AXmessagePayload *dispose = NULL;
  while (events.read != events.write) axTakeMessagePayload(&events.queue[events.read++], &dispose);
  axDisposePayloads(dispose);
}

void
axRemoveMessages(void *hobj, uint32_t event, uint32_t wparam)
{
  struct AXmessagePayload *dispose = NULL;
  uint16_t read = events.read, write = events.read;
  while (read != events.write) {
    struct AXmessage *message = &events.queue[read++];
    if (message->target == hobj && (!event || message->message == event) &&
        (wparam == AX_ANY_WPARAM || message->wParam == wparam))
      axTakeMessagePayload(message, &dispose);
    else events.queue[write++] = *message;
  }
  events.write = write;
  axDisposePayloads(dispose);
}

void
axRemoveFromQueue(void *hobj)
{
  axRemoveMessages(hobj, 0, AX_ANY_WPARAM);
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id && s_timers[i].obj == hobj) axCancelTimer(s_timers[i].id);
}

void
axNotifyFileDropEvent(char const *filename, float x, float y)
{
  if (!filename || !filename[0]) return;
  char *path = strdup(filename);
  if (path) axPostMessageW(NULL, kEventDragDrop, MAKEDWORD((uint16_t)x, (uint16_t)y), path);
}

/* ── activity lifecycle ─────────────────────────────────────────────────── */

void
android_handle_cmd(struct android_app *app, int32_t cmd)
{
  (void)app;
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      android_surface_create();
      break;
    case APP_CMD_TERM_WINDOW:
      android_surface_destroy();
      break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
      if (android.surface != EGL_NO_SURFACE) {
        eglQuerySurface(android.display, android.surface, EGL_WIDTH, &android.pixel_width);
        eglQuerySurface(android.display, android.surface, EGL_HEIGHT, &android.pixel_height);
      }
      axPostMessageW(NULL, kEventWindowResized, android_point_size(), NULL);
      axPostMessageW(NULL, kEventWindowPaint, android_point_size(), NULL);
      break;
    case APP_CMD_WINDOW_REDRAW_NEEDED:
      axPostMessageW(NULL, kEventWindowPaint, android_point_size(), NULL);
      break;
    case APP_CMD_CONFIG_CHANGED: {
      bool_t dark = axIsDarkTheme();
      AConfiguration_fromAssetManager(android.app->config, android.app->activity->assetManager);
      android_update_scale();
      if (dark != axIsDarkTheme()) axPostMessageW(NULL, kEventAppearanceChanged, axIsDarkTheme(), NULL);
      axPostMessageW(NULL, kEventWindowResized, android_point_size(), NULL);
      axPostMessageW(NULL, kEventWindowPaint, android_point_size(), NULL);
      break;
    }
    case APP_CMD_GAINED_FOCUS:
      android.focused = TRUE;
      axPostMessageW(NULL, kEventSetFocus, 0, NULL);
      axPostMessageW(NULL, kEventWindowPaint, android_point_size(), NULL);
      break;
    case APP_CMD_LOST_FOCUS:
      android.focused = FALSE;
      axPostMessageW(NULL, kEventKillFocus, 0, NULL);
      break;
    case APP_CMD_DESTROY:
      android.destroy = TRUE;
      axPostMessageW(NULL, kEventWindowClosed, 0, NULL);
      break;
    default:
      break;
  }
}

bool_t axAndroidDestroyRequested(void) { return android.destroy; }

/* ── input ──────────────────────────────────────────────────────────────── */

static uint32_t
android_key(int32_t code)
{
  switch (code) {
    case AKEYCODE_DEL: return AX_KEY_BACKSPACE;
    case AKEYCODE_FORWARD_DEL: return AX_KEY_DEL;
    case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: return AX_KEY_ENTER;
    case AKEYCODE_TAB: return AX_KEY_TAB;
    case AKEYCODE_ESCAPE: return AX_KEY_ESCAPE;
    case AKEYCODE_DPAD_LEFT: return AX_KEY_LEFTARROW;
    case AKEYCODE_DPAD_RIGHT: return AX_KEY_RIGHTARROW;
    case AKEYCODE_DPAD_UP: return AX_KEY_UPARROW;
    case AKEYCODE_DPAD_DOWN: return AX_KEY_DOWNARROW;
    default: return 0; /* Text arrives as committed strings from the input view. */
  }
}

static uint32_t
android_modifiers(int32_t meta)
{
  uint32_t mods = 0;
  if (meta & AMETA_SHIFT_ON) mods |= AX_MOD_SHIFT;
  if (meta & AMETA_CTRL_ON) mods |= AX_MOD_CTRL;
  if (meta & AMETA_ALT_ON) mods |= AX_MOD_ALT;
  if (meta & AMETA_META_ON) mods |= AX_MOD_CMD;
  return mods;
}

static uint32_t
android_point(AInputEvent *event, size_t index)
{
  float x = AMotionEvent_getX(event, index) / android.scale, y = AMotionEvent_getY(event, index) / android.scale;
  return MAKEDWORD((int)CLAMP(x, 0, UINT16_MAX), (int)CLAMP(y, 0, UINT16_MAX));
}

int32_t
android_handle_input(struct android_app *app, AInputEvent *event)
{
  (void)app;
  static float last_x, last_y;
  static int64_t last_tap;
  static int32_t active = -1;
  if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
    uint32_t key = android_key(AKeyEvent_getKeyCode(event));
    if (!key) return 0;
    int32_t action = AKeyEvent_getAction(event);
    if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) return 0;
    axPostMessageW(NULL, action == AKEY_EVENT_ACTION_DOWN ? kEventKeyDown : kEventKeyUp,
                   key | android_modifiers(AKeyEvent_getMetaState(event)), NULL);
    return 1;
  }
  if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
  int32_t action = AMotionEvent_getAction(event), kind = action & AMOTION_EVENT_ACTION_MASK;
  size_t index = (size_t)((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
  int32_t tool = AMotionEvent_getToolType(event, index);
  ax_pointer_t pointer = {
    .flags = tool == AMOTION_EVENT_TOOL_TYPE_STYLUS ? AX_POINTER_STYLUS : tool == AMOTION_EVENT_TOOL_TYPE_FINGER ? AX_POINTER_TOUCH : 0,
    .time = (uint32_t)(AMotionEvent_getEventTime(event) / 1000000),
  };
  if (pointer.flags & AX_POINTER_STYLUS)
    pointer.altitude = 1.5707964f - AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_TILT, index);
  /* One active pointer drives the mouse-style stream, like the iOS backend. */
  switch (kind) {
    case AMOTION_EVENT_ACTION_DOWN: {
      active = AMotionEvent_getPointerId(event, index);
      uint32_t at = android_point(event, index);
      float x = LOWORD(at), y = HIWORD(at);
      int64_t now = AMotionEvent_getEventTime(event) / 1000000;
      axPostMessageOwnedW((struct AXmessage){ .message = kEventLeftButtonDown, .wParam = at, .pointer = pointer });
      if (now - last_tap <= 300 && (x - last_x) * (x - last_x) + (y - last_y) * (y - last_y) <= 400) {
        axPostMessageOwnedW((struct AXmessage){ .message = kEventLeftDoubleClick, .wParam = at, .pointer = pointer });
        last_tap = 0;
      } else last_tap = now;
      last_x = x; last_y = y;
      return 1;
    }
    case AMOTION_EVENT_ACTION_MOVE: {
      for (size_t i = 0, n = AMotionEvent_getPointerCount(event); i < n; i++) {
        if (AMotionEvent_getPointerId(event, i) != active) continue;
        uint32_t at = android_point(event, i);
        float x = LOWORD(at), y = HIWORD(at);
        axPostMessageOwnedW((struct AXmessage){ .message = kEventLeftButtonDragged, .wParam = at,
          .dx = (int16_t)(x - last_x), .dy = (int16_t)(y - last_y), .pointer = pointer });
        last_x = x; last_y = y;
      }
      return 1;
    }
    case AMOTION_EVENT_ACTION_POINTER_UP:
      if (AMotionEvent_getPointerId(event, index) != active) return 1;
      /* fall through: the active finger lifted */
    case AMOTION_EVENT_ACTION_UP:
      if (active < 0) return 1;
      active = -1;
      axPostMessageOwnedW((struct AXmessage){ .message = kEventLeftButtonUp, .wParam = android_point(event, index), .pointer = pointer });
      return 1;
    case AMOTION_EVENT_ACTION_CANCEL:
      if (active < 0) return 1;
      active = -1;
      axPostMessageOwnedW((struct AXmessage){ .message = kEventPointerCancel, .pointer = pointer });
      return 1;
    case AMOTION_EVENT_ACTION_SCROLL: {
      float dy = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, index);
      float dx = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, index);
      axPostMessageOwnedW((struct AXmessage){ .message = kEventScrollWheel, .wParam = android_point(event, index),
        .dx = (int16_t)dx, .dy = (int16_t)dy });
      return 1;
    }
    default:
      return 0;
  }
}

void
axAndroidSetApp(struct android_app *app)
{
  android.app = app;
  app->onAppCmd = android_handle_cmd;
  app->onInputEvent = android_handle_input;
  if (inbox_pipe[0] < 0 && pipe(inbox_pipe) == 0) {
    for (int i = 0; i < 2; i++) {
      fcntl(inbox_pipe[i], F_SETFL, fcntl(inbox_pipe[i], F_GETFL) | O_NONBLOCK);
      fcntl(inbox_pipe[i], F_SETFD, FD_CLOEXEC);
    }
    ALooper_addFd(app->looper, inbox_pipe[0], ANDROID_LOOPER_INBOX, ALOOPER_EVENT_INPUT, NULL, NULL);
  }
}

struct android_app *axAndroidGetApp(void) { return android.app; }
