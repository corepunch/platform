#include "windows_local.h"
#include "../platform.h"

/* Internal event ring-buffer (power-of-two size so index wrap is a mask) */
static struct
{
  EVENT    queue[0x10000];
  uint16_t write, read;
  float    pointer_x, pointer_y;
} events = { 0 };

/* Previous mouse position for computing drag deltas */
static int16_t s_last_mouse_x = 0;
static int16_t s_last_mouse_y = 0;

#define MAX_TIMERS 64
static struct {
  uint32_t id;
  UINT_PTR win_id;
  void*    obj;
  void*    userdata;
  bool_t   repeat;
} s_timers[MAX_TIMERS];
static uint32_t s_next_timer_id = 1;

static void CALLBACK
timer_proc(HWND h, UINT m, UINT_PTR nid, DWORD t)
{
  (void)h; (void)m; (void)t;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].win_id == nid) {
      events.queue[events.write++] = (EVENT){
        .target  = s_timers[i].obj,
        .message = kEventTimer,
        .wParam  = s_timers[i].id,
        .lParam  = s_timers[i].userdata,
      };
      if (!s_timers[i].repeat)
        axCancelTimer(s_timers[i].id);
      return;
    }
  }
}

/* Map a Win32 virtual-key code to a AX_KEY_* constant.
   Extended-key flag (bit 24 of the LPARAM) is passed in ext_key
   so that numpad-Enter can be distinguished from regular Enter. */
static uint32_t
vk_to_axkey(WPARAM vk, int ext_key)
{
  switch (vk) {
  case VK_TAB:      return AX_KEY_TAB;
  case VK_RETURN:   return ext_key ? AX_KEY_KP_ENTER : AX_KEY_ENTER;
  case VK_ESCAPE:   return AX_KEY_ESCAPE;
  case VK_SPACE:    return AX_KEY_SPACE;
  case VK_BACK:     return AX_KEY_BACKSPACE;

  case VK_UP:       return AX_KEY_UPARROW;
  case VK_DOWN:     return AX_KEY_DOWNARROW;
  case VK_LEFT:     return AX_KEY_LEFTARROW;
  case VK_RIGHT:    return AX_KEY_RIGHTARROW;

  case VK_SHIFT:
  case VK_LSHIFT:
  case VK_RSHIFT:   return AX_KEY_SHIFT;
  case VK_CONTROL:
  case VK_LCONTROL:
  case VK_RCONTROL: return AX_KEY_CTRL;
  case VK_MENU:
  case VK_LMENU:
  case VK_RMENU:    return AX_KEY_ALT;

  case VK_F1:       return AX_KEY_F1;
  case VK_F2:       return AX_KEY_F2;
  case VK_F3:       return AX_KEY_F3;
  case VK_F4:       return AX_KEY_F4;
  case VK_F5:       return AX_KEY_F5;
  case VK_F6:       return AX_KEY_F6;
  case VK_F7:       return AX_KEY_F7;
  case VK_F8:       return AX_KEY_F8;
  case VK_F9:       return AX_KEY_F9;
  case VK_F10:      return AX_KEY_F10;
  case VK_F11:      return AX_KEY_F11;
  case VK_F12:      return AX_KEY_F12;

  case VK_INSERT:   return AX_KEY_INS;
  case VK_DELETE:   return AX_KEY_DEL;
  case VK_PRIOR:    return AX_KEY_PGUP;
  case VK_NEXT:     return AX_KEY_PGDN;
  case VK_HOME:     return AX_KEY_HOME;
  case VK_END:      return AX_KEY_END;
  case VK_PAUSE:    return AX_KEY_PAUSE;

  /* Numpad (num-lock off — cursor keys)  */
  case VK_NUMPAD7:  return AX_KEY_KP_HOME;
  case VK_NUMPAD8:  return AX_KEY_KP_UPARROW;
  case VK_NUMPAD9:  return AX_KEY_KP_PGUP;
  case VK_NUMPAD4:  return AX_KEY_KP_LEFTARROW;
  case VK_NUMPAD5:  return AX_KEY_KP_5;
  case VK_NUMPAD6:  return AX_KEY_KP_RIGHTARROW;
  case VK_NUMPAD1:  return AX_KEY_KP_END;
  case VK_NUMPAD2:  return AX_KEY_KP_DOWNARROW;
  case VK_NUMPAD3:  return AX_KEY_KP_PGDN;
  case VK_NUMPAD0:  return AX_KEY_KP_INS;
  case VK_DECIMAL:  return AX_KEY_KP_DEL;
  case VK_DIVIDE:   return AX_KEY_KP_SLASH;
  case VK_SUBTRACT: return AX_KEY_KP_MINUS;
  case VK_ADD:      return AX_KEY_KP_PLUS;

  default:
    /* Letters and digits share their AX_KEY value with ASCII */
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
      return (uint32_t)vk;
    }
    return 0;
  }
}

/* Collect modifier flags from the current keyboard state */
static uint32_t
win32_modifiers(void)
{
  uint32_t mods = 0;
  if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= AX_MOD_SHIFT;
  if (GetKeyState(VK_CONTROL) & 0x8000) mods |= AX_MOD_CTRL;
  if (GetKeyState(VK_MENU)    & 0x8000) mods |= AX_MOD_ALT;
  if (GetKeyState(VK_LWIN)    & 0x8000) mods |= AX_MOD_CMD;
  if (GetKeyState(VK_RWIN)    & 0x8000) mods |= AX_MOD_CMD;
  return mods;
}

/* Win32 window procedure — translates native messages into AX events */
LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  switch (msg) {

  case WM_KEYDOWN:
  case WM_SYSKEYDOWN: {
    int ext = (lparam >> 24) & 1; /* extended-key flag */
    uint32_t key = vk_to_axkey(wparam, ext);
    if (key) {
      events.queue[events.write++] = (EVENT){
        .message = kEventKeyDown,
        .wParam  = key | win32_modifiers(),
      };
    }
    break;
  }

  case WM_KEYUP:
  case WM_SYSKEYUP: {
    int ext = (lparam >> 24) & 1;
    uint32_t key = vk_to_axkey(wparam, ext);
    if (key) {
      events.queue[events.write++] = (EVENT){
        .message = kEventKeyUp,
        .wParam  = key | win32_modifiers(),
      };
    }
    break;
  }

  case WM_CHAR: {
    uint32_t ch = (uint32_t)wparam;
    if (ch >= 32 && ch < 127) {
      events.queue[events.write++] = (EVENT){
        .message = kEventChar,
        .wParam  = ch,
      };
    }
    break;
  }

  case WM_LBUTTONDOWN:
    SetCapture(hwnd);
    events.queue[events.write++] = (EVENT){
      .message = kEventLeftButtonDown,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_LBUTTONUP:
    ReleaseCapture();
    events.queue[events.write++] = (EVENT){
      .message = kEventLeftButtonUp,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_LBUTTONDBLCLK:
    events.queue[events.write++] = (EVENT){
      .message = kEventLeftButtonDown,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    events.queue[events.write++] = (EVENT){
      .message = kEventLeftDoubleClick,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_RBUTTONDOWN:
    events.queue[events.write++] = (EVENT){
      .message = kEventRightButtonDown,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_RBUTTONUP:
    events.queue[events.write++] = (EVENT){
      .message = kEventRightButtonUp,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_MBUTTONDOWN:
    events.queue[events.write++] = (EVENT){
      .message = kEventOtherButtonDown,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_MBUTTONUP:
    events.queue[events.write++] = (EVENT){
      .message = kEventOtherButtonUp,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_RBUTTONDBLCLK:
    events.queue[events.write++] = (EVENT){
      .message = kEventRightButtonDown,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    events.queue[events.write++] = (EVENT){
      .message = kEventRightDoubleClick,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_MBUTTONDBLCLK:
    events.queue[events.write++] = (EVENT){
      .message = kEventOtherButtonDown,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    events.queue[events.write++] = (EVENT){
      .message = kEventOtherDoubleClick,
      .x = (uint16_t)LOWORD(lparam),
      .y = (uint16_t)HIWORD(lparam),
    };
    break;

  case WM_MOUSEMOVE: {
    int16_t cur_x = (int16_t)LOWORD(lparam);
    int16_t cur_y = (int16_t)HIWORD(lparam);
    int16_t dx = cur_x - s_last_mouse_x;
    int16_t dy = cur_y - s_last_mouse_y;
    s_last_mouse_x = cur_x;
    s_last_mouse_y = cur_y;
    events.pointer_x = (float)cur_x;
    events.pointer_y = (float)cur_y;

    uint32_t msg;
    if (wparam & MK_LBUTTON)
      msg = kEventLeftButtonDragged;
    else if (wparam & MK_RBUTTON)
      msg = kEventRightButtonDragged;
    else if (wparam & MK_MBUTTON)
      msg = kEventOtherButtonDragged;
    else
      msg = kEventMouseMoved;

    events.queue[events.write++] = (EVENT){
      .message = msg,
      .x = (uint16_t)cur_x,
      .y = (uint16_t)cur_y,
      .dx = dx,
      .dy = dy,
    };
    break;
  }

  case WM_MOUSEWHEEL: {
    short delta = (short)HIWORD(wparam);
    int16_t dy  = (delta > 0) ? -1 : 1;
    /* Convert screen coords to client coords for the position */
    POINT pt = { LOWORD(lparam), HIWORD(lparam) };
    ScreenToClient(hwnd, &pt);
    events.queue[events.write++] = (EVENT){
      .message = kEventScrollWheel,
      .x  = (uint16_t)pt.x,
      .y  = (uint16_t)pt.y,
      .dy = dy,
    };
    break;
  }

  case WM_SIZE: {
    int nw = (int)LOWORD(lparam);
    int nh = (int)HIWORD(lparam);
    if (nw != g_win_width || nh != g_win_height) {
      g_win_width  = nw;
      g_win_height = nh;
      events.queue[events.write++] = (EVENT){
        .message = kEventWindowResized,
        .wParam  = MAKEDWORD(nw, nh),
      };
    }
    break;
  }

  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    events.queue[events.write++] = (EVENT){
      .message = kEventWindowPaint,
    };
    break;
  }

  case WM_SETFOCUS:
    events.queue[events.write++] = (EVENT){
      .message = kEventSetFocus,
    };
    break;

  case WM_KILLFOCUS:
    events.queue[events.write++] = (EVENT){
      .message = kEventKillFocus,
    };
    break;

  case WM_CLOSE:
    events.queue[events.write++] = (EVENT){
      .message = kEventWindowClosed,
    };
    /* Do not call DestroyWindow; let the application decide */
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  default:
    break;
  }

  return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/* Pump the Win32 message queue, pushing translated events onto the ring buffer */
static void
win32_process_messages(void)
{
  MSG msg;
  while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
}

/* Defined in windows_joystick.c */
extern void joy_poll(void);

int
axWaitMessage(longTime_t timeout_ms)
{
  if (timeout_ms == 0) {
    /* Block indefinitely, but wake on joystick input too.  Poll joystick
     * at ~60 Hz so a controller-only application doesn't spin at 100% CPU
     * yet still responds promptly. */
    for (;;) {
      DWORD ret = MsgWaitForMultipleObjects(0, NULL, FALSE, 16 /* ms */, QS_ALLINPUT);
      win32_process_messages();
      joy_poll();
      if (events.read != events.write) {
        return 1;
      }
      if (ret == WAIT_OBJECT_0) {
        return 1;
      }
    }
  }
  DWORD ret = MsgWaitForMultipleObjects(0, NULL, FALSE,
                                         (DWORD)timeout_ms, QS_ALLINPUT);
  win32_process_messages();
  joy_poll();
  return (ret == WAIT_OBJECT_0 || events.read != events.write) ? 1 : 0;
}

int
axPeekMessage(PEVENT pEvent)
{
  joy_poll();
  win32_process_messages();

  if (events.read != events.write) {
    *pEvent = events.queue[events.read++];
    return 1;
  }
  return 0;
}

int
axGetMessage(PEVENT pEvent)
{
  if (axPeekMessage(pEvent)) {
    return 1;
  }

  for (;;) {
    if (axWaitMessage(0) <= 0) {
      continue;
    }
    if (axPeekMessage(pEvent)) {
      return 1;
    }
  }
}

void
axPostMessageW(void *hobj, uint32_t event, uint32_t wparam, void *lparam)
{
  if (events.write - events.read >=
      (int)(sizeof(events.queue) / sizeof(events.queue[0]))) {
    return;
  }
  events.queue[events.write++] = (EVENT){
    .target  = hobj,
    .message = event,
    .wParam  = wparam,
    .lParam  = lparam,
  };
}

void
axRemoveFromQueue(void *hobj)
{
  uint16_t r = events.read;
  uint16_t w = events.write;
  uint16_t nw = events.read;

  while (r != w) {
    if (events.queue[r].target != hobj)
      events.queue[nw++] = events.queue[r];
    r++;
  }
  events.write = nw;
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

uint32_t
axSetTimer(void* obj, uint32_t interval_ms, void* userdata, bool_t repeat)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == 0) {
      uint32_t tid = s_next_timer_id++;
      UINT_PTR wid = SetTimer(g_hwnd, (UINT_PTR)tid, interval_ms, timer_proc);
      if (wid == 0) return 0;
      s_timers[i].id       = tid;
      s_timers[i].win_id   = wid;
      s_timers[i].obj      = obj;
      s_timers[i].userdata = userdata;
      s_timers[i].repeat   = repeat;
      return tid;
    }
  }
  return 0;
}

void
axCancelTimer(uint32_t timer_id)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == timer_id) {
      KillTimer(g_hwnd, s_timers[i].win_id);
      s_timers[i].id       = 0;
      s_timers[i].win_id   = 0;
      s_timers[i].obj      = NULL;
      s_timers[i].userdata = NULL;
      s_timers[i].repeat   = FALSE;
      return;
    }
  }
}
