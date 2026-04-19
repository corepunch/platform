#include "../platform.h"
#include "webgl_local.h"

static struct
{
  EVENT queue[0x10000];
  WORD write, read;
  float pointer_x, pointer_y;
  uint32_t buttons; /* bitmask of currently pressed mouse buttons */
} events = { 0 };

#define MAX_TIMERS 64
static struct {
  uint32_t id;
  int      js_id;
  void*    obj;
  void*    userdata;
  bool_t   repeat;
} s_timers[MAX_TIMERS];
static uint32_t s_next_timer_id = 1;

static uint32_t
map_keycode(unsigned long keyCode)
{
  switch (keyCode) {
  case 8:   return AX_KEY_BACKSPACE;
  case 9:   return AX_KEY_TAB;
  case 13:  return AX_KEY_ENTER;
  case 16:  return AX_KEY_SHIFT;
  case 17:  return AX_KEY_CTRL;
  case 18:  return AX_KEY_ALT;
  case 27:  return AX_KEY_ESCAPE;
  case 32:  return AX_KEY_SPACE;
  case 33:  return AX_KEY_PGUP;
  case 34:  return AX_KEY_PGDN;
  case 35:  return AX_KEY_END;
  case 36:  return AX_KEY_HOME;
  case 37:  return AX_KEY_LEFTARROW;
  case 38:  return AX_KEY_UPARROW;
  case 39:  return AX_KEY_RIGHTARROW;
  case 40:  return AX_KEY_DOWNARROW;
  case 45:  return AX_KEY_INS;
  case 46:  return AX_KEY_DEL;
  case 112: return AX_KEY_F1;
  case 113: return AX_KEY_F2;
  case 114: return AX_KEY_F3;
  case 115: return AX_KEY_F4;
  case 116: return AX_KEY_F5;
  case 117: return AX_KEY_F6;
  case 118: return AX_KEY_F7;
  case 119: return AX_KEY_F8;
  case 120: return AX_KEY_F9;
  case 121: return AX_KEY_F10;
  case 122: return AX_KEY_F11;
  case 123: return AX_KEY_F12;
  default:
    if (keyCode >= 65 && keyCode <= 90) {
      return (uint32_t)tolower((int)keyCode); // A-Z -> a-z
    }
    return (uint32_t)keyCode;
  }
}

static EM_BOOL
on_mousemove(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  int16_t dx = (int16_t)(e->targetX - (int)events.pointer_x);
  int16_t dy = (int16_t)(e->targetY - (int)events.pointer_y);
  events.pointer_x = (float)e->targetX;
  events.pointer_y = (float)e->targetY;

  uint32_t msg;
  if (events.buttons & (1u << 0))
    msg = kEventLeftButtonDragged;
  else if (events.buttons & (1u << 2))
    msg = kEventRightButtonDragged;
  else if (events.buttons & (1u << 1))
    msg = kEventOtherButtonDragged;
  else
    msg = kEventMouseMoved;

  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)e->targetX,
    .y = (uint16_t)e->targetY,
    .dx = dx,
    .dy = dy,
    .message = msg,
  };
  return EM_TRUE;
}

static EM_BOOL
on_mousedown(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  events.buttons |= (1u << e->button);
  uint32_t msg;
  switch (e->button) {
  case 0:  msg = kEventLeftButtonDown;  break;
  case 2:  msg = kEventRightButtonDown; break;
  default: msg = kEventOtherButtonDown; break;
  }
  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)e->targetX,
    .y = (uint16_t)e->targetY,
    .message = msg,
  };
  return EM_TRUE;
}

static EM_BOOL
on_mouseup(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  events.buttons &= ~(1u << e->button);
  uint32_t msg;
  switch (e->button) {
  case 0:  msg = kEventLeftButtonUp;  break;
  case 2:  msg = kEventRightButtonUp; break;
  default: msg = kEventOtherButtonUp; break;
  }
  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)e->targetX,
    .y = (uint16_t)e->targetY,
    .message = msg,
  };
  return EM_TRUE;
}

// static EM_BOOL
// on_dblclick(int eventType, const EmscriptenMouseEvent *e, void *userData)
// {
//   (void)eventType;
//   (void)userData;
//   uint32_t msg;
//   switch (e->button) {
//   case 0:  msg = kEventLeftDoubleClick;  break;
//   case 2:  msg = kEventRightDoubleClick; break;
//   default: msg = kEventOtherDoubleClick; break;
//   }
//   events.queue[events.write++] = (EVENT){
//     .x = (uint16_t)e->targetX,
//     .y = (uint16_t)e->targetY,
//     .message = msg,
//   };
//   return EM_TRUE;
// }

static EM_BOOL
on_wheel(int eventType, const EmscriptenWheelEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  int16_t dx = -(int16_t)e->deltaX;
  int16_t dy = -(int16_t)e->deltaY;
  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)events.pointer_x,
    .y = (uint16_t)events.pointer_y,
    .lParam = (void *)(intptr_t)((dy << 16) | (uint16_t)dx),
    .message = kEventScrollWheel,
  };
  return EM_TRUE;
}

static EM_BOOL
on_keydown(int eventType, const EmscriptenKeyboardEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  uint32_t keycode = map_keycode(e->keyCode);
  events.queue[events.write++] = (EVENT){
    .message = kEventKeyDown,
    .wParam = keycode,
  };
  return EM_FALSE; // allow default browser behaviour
}

static EM_BOOL
on_keyup(int eventType, const EmscriptenKeyboardEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  uint32_t keycode = map_keycode(e->keyCode);
  events.queue[events.write++] = (EVENT){
    .message = kEventKeyUp,
    .wParam = keycode,
  };
  return EM_FALSE;
}

static EM_BOOL
on_touchstart(int eventType, const EmscriptenTouchEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  if (e->numTouches < 1) return EM_TRUE;
  const EmscriptenTouchPoint *t = &e->touches[0];
  int tx = (int)t->targetX;
  int ty = (int)t->targetY;
  events.buttons |= (1u << 0);
  events.pointer_x = (float)tx;
  events.pointer_y = (float)ty;
  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)tx,
    .y = (uint16_t)ty,
    .message = kEventLeftButtonDown,
  };
  return EM_TRUE;
}

static EM_BOOL
on_touchmove(int eventType, const EmscriptenTouchEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  if (e->numTouches < 1) return EM_TRUE;
  const EmscriptenTouchPoint *t = &e->touches[0];
  int tx = (int)t->targetX;
  int ty = (int)t->targetY;
  int16_t dx = (int16_t)(tx - (int)events.pointer_x);
  int16_t dy = (int16_t)(ty - (int)events.pointer_y);
  events.pointer_x = (float)tx;
  events.pointer_y = (float)ty;
  // /* kEventLeftButtonDragged — for drag-responsive UI (tap-to-drag scroll) */
  // events.queue[events.write++] = (EVENT){
  //   .x = (uint16_t)tx,
  //   .y = (uint16_t)ty,
  //   .dx = dx,
  //   .dy = dy,
  //   .message = kEventLeftButtonDragged,
  // };
  int16_t sdx = (int16_t)(dx);
  int16_t sdy = (int16_t)(dy);
  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)tx,
    .y = (uint16_t)ty,
    .lParam = (void *)(intptr_t)((sdy << 16) | (uint16_t)sdx),
    .message = kEventScrollWheel,
  };
  return EM_TRUE;
}

static EM_BOOL
on_touchend(int eventType, const EmscriptenTouchEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  events.buttons &= ~(1u << 0);
  uint16_t x = (uint16_t)events.pointer_x;
  uint16_t y = (uint16_t)events.pointer_y;
  if (e->numTouches >= 1) {
    x = (uint16_t)e->touches[0].targetX;
    y = (uint16_t)e->touches[0].targetY;
  }
  events.queue[events.write++] = (EVENT){
    .x = x,
    .y = y,
    .message = kEventLeftButtonUp,
  };
  return EM_TRUE;
}

static EM_BOOL
on_resize(int eventType, const EmscriptenUiEvent *e, void *userData)
{
  (void)eventType;
  (void)userData;
  (void)e;

  /* Get the canvas element's actual CSS display size */
  double css_width, css_height;
  emscripten_get_element_css_size("#canvas", &css_width, &css_height);

  /* Skip transient zero/negative dimensions (e.g. mid-orientation animation) */
  if (css_width <= 0 || css_height <= 0)
    return EM_TRUE;

  /* Skip if dimensions are unchanged (deduplicates burst of resize events during rotation) */
  if ((int)css_width == g_canvas_width && (int)css_height == g_canvas_height)
    return EM_TRUE;

  /* Truncate CSS size to integer before multiplying by DPR so that
   * physical_pixels / dpr is always an integer, preventing fractional
   * touch targetX/targetY values on high-DPI devices (e.g. DPR=3 on iPhone). */
  g_canvas_width  = (int)css_width;
  g_canvas_height = (int)css_height;

  /* Resize the canvas drawing buffer */
  emscripten_set_canvas_element_size("#canvas",
    (int)(g_canvas_width  * axGetScaling() + 0.5),
    (int)(g_canvas_height * axGetScaling() + 0.5));

  /* Notify the engine with the canvas size (not window size) */

  events.queue[events.write++] = (EVENT){
    .message = kEventWindowResized,
    .wParam = MAKEDWORD(g_canvas_width, g_canvas_height),
  };
  return EM_TRUE;
}

void
webgl_register_callbacks(void)
{
  emscripten_set_mousemove_callback("#canvas", NULL, EM_FALSE, on_mousemove);
  emscripten_set_mousedown_callback("#canvas", NULL, EM_FALSE, on_mousedown);
  emscripten_set_mouseup_callback("#canvas", NULL, EM_FALSE, on_mouseup);
  // emscripten_set_dblclick_callback("#canvas", NULL, EM_FALSE, on_dblclick);
  emscripten_set_wheel_callback("#canvas", NULL, EM_FALSE, on_wheel);
  emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_FALSE, on_keydown);
  emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_FALSE, on_keyup);
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_FALSE, on_resize);
  emscripten_set_touchstart_callback("#canvas", NULL, EM_FALSE, on_touchstart);
  emscripten_set_touchmove_callback("#canvas", NULL, EM_FALSE, on_touchmove);
  emscripten_set_touchend_callback("#canvas", NULL, EM_FALSE, on_touchend);
}

int
axWaitEvent(TIME time)
{
  (void)time;
  // In the browser all events arrive via registered callbacks; nothing to block on.
  return 0;
}

int
axPollEvent(PEVENT pEvent)
{
  if (events.read != events.write) {
    *pEvent = events.queue[events.read++];
    return 1;
  }
  return 0;
}

void
axPostMessageW(void *hobj, uint32_t event, uint32_t wparam, void *lparam)
{
  if ((WORD)(events.write - events.read) == (WORD)(sizeof(events.queue) / sizeof(events.queue[0]) - 1)) {
    return;
  }
  events.queue[events.write++] = (EVENT){
    .target = hobj,
    .message = event,
    .wParam = wparam,
    .lParam = lparam,
  };
}

void
axRemoveFromQueue(void *hobj)
{
  WORD read_idx = events.read;
  WORD write_idx = events.write;
  WORD new_write = events.read;

  while (read_idx != write_idx) {
    if (events.queue[read_idx].target != hobj)
      events.queue[new_write++] = events.queue[read_idx];
    read_idx++;
  }
  events.write = new_write;
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id != 0 && s_timers[i].obj == hobj)
      axCancelTimer(s_timers[i].id);
}

void
axNotifyFileDropEvent(char const *filename, float x, float y)
{
  (void)filename;
  (void)x;
  (void)y;
}

/* Called from JavaScript when a timer fires */
EMSCRIPTEN_KEEPALIVE
void
timer_fired(uint32_t timer_id)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == timer_id) {
      events.queue[events.write++] = (EVENT){
        .target  = s_timers[i].obj,
        .message = kEventTimer,
        .wParam  = timer_id,
        .lParam  = s_timers[i].userdata,
      };
      if (!s_timers[i].repeat)
        axCancelTimer(timer_id);
      return;
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
  int js_id = EM_ASM_INT({
    var fn = function() { _timer_fired($0); };
    return $2 ? setInterval(fn, $1) : setTimeout(fn, $1);
  }, tid, (int)interval_ms, (int)repeat);
  s_timers[slot].id       = tid;
  s_timers[slot].js_id    = js_id;
  s_timers[slot].obj      = obj;
  s_timers[slot].userdata = userdata;
  s_timers[slot].repeat   = repeat;
  return tid;
}

void
axCancelTimer(uint32_t timer_id)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == timer_id) {
      EM_ASM({
        if ($1) clearInterval($0); else clearTimeout($0);
      }, s_timers[i].js_id, (int)s_timers[i].repeat);
      s_timers[i].id       = 0;
      s_timers[i].js_id    = 0;
      s_timers[i].obj      = NULL;
      s_timers[i].userdata = NULL;
      s_timers[i].repeat   = FALSE;
      return;
    }
  }
}
