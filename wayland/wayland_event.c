#include "../platform.h"

#include "wayland_local.h"
#include <unistd.h>
#include <poll.h>
#include <linux/input-event-codes.h>
#include <sys/timerfd.h>

/* Defined in unix/unix_joystick.c */
extern void joy_poll(void);
extern int  joy_get_fd(void);

static struct
{
  EVENT queue[0x10000];
  WORD write, read;
  VECTOR2D pointer;
  uint32_t buttons;        /* bitmask of currently pressed buttons */
  uint32_t mods;           /* current AX_MOD_* modifier flags */
  uint32_t last_btn;       /* button of last click (for double-click) */
  uint32_t last_btn_time;  /* timestamp of last click (ms) */
} events = { 0 };

#define MAX_TIMERS 64
static struct {
  uint32_t id;
  int      fd;
  void*    obj;
  void*    userdata;
  bool_t   repeat;
} s_timers[MAX_TIMERS];
static uint32_t s_next_timer_id = 1;

/* Check all timerfd descriptors for readability and post kEventTimer */
static void
timers_poll(void)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == 0)
      continue;
    uint64_t expirations = 0;
    ssize_t n = read(s_timers[i].fd, &expirations, sizeof(expirations));
    if (n == (ssize_t)sizeof(expirations) && expirations > 0) {
      events.queue[events.write++] = (EVENT){
        .target  = s_timers[i].obj,
        .message = kEventTimer,
        .wParam  = s_timers[i].id,
        .lParam  = s_timers[i].userdata,
      };
      if (!s_timers[i].repeat)
        axCancelTimer(s_timers[i].id);
    }
  }
}

extern struct xkb_state* xkb_state;

static struct wl_pointer* pointer = NULL;
static struct wl_keyboard* keyboard = NULL;

static void
pointer_enter(void* data,
              struct wl_pointer* pointer,
              uint32_t serial,
              struct wl_surface* surface,
              wl_fixed_t sx,
              wl_fixed_t sy)
{
}

static void
pointer_leave(void* data,
              struct wl_pointer* pointer,
              uint32_t serial,
              struct wl_surface* surface)
{
}

static void
pointer_motion(void* data,
               struct wl_pointer* pointer,
               uint32_t time,
               wl_fixed_t sx,
               wl_fixed_t sy)
{
  float new_x = wl_fixed_to_double(sx);
  float new_y = wl_fixed_to_double(sy);
  int16_t dx = (int16_t)(new_x - events.pointer.x);
  int16_t dy = (int16_t)(new_y - events.pointer.y);
  events.pointer.x = new_x;
  events.pointer.y = new_y;

  uint32_t msg;
  if (events.buttons & 1u)                                   /* BTN_LEFT   */
    msg = kEventLeftButtonDragged;
  else if (events.buttons & (1u << (BTN_RIGHT - BTN_LEFT)))  /* BTN_RIGHT  */
    msg = kEventRightButtonDragged;
  else if (events.buttons & (1u << (BTN_MIDDLE - BTN_LEFT))) /* BTN_MIDDLE */
    msg = kEventOtherButtonDragged;
  else
    msg = kEventMouseMoved;

  events.queue[events.write++] = (EVENT){
    .x = (uint16_t)new_x,
    .y = (uint16_t)new_y,
    .dx = dx,
    .dy = dy,
    .message = msg,
  };
}

static void
pointer_button(void* data,
               struct wl_pointer* pointer,
               uint32_t serial,
               uint32_t time,
               uint32_t button,
               uint32_t state)
{
  bool_t pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;

  /* Track button bitmask (offset from BTN_LEFT for compact storage) */
  uint32_t bit = 1u << (button - BTN_LEFT);
  if (pressed)
    events.buttons |= bit;
  else
    events.buttons &= ~bit;

  /* Determine event type, checking for double-click */
  uint32_t down_msg, up_msg, dbl_msg;
  switch (button) {
  case BTN_LEFT:
    down_msg = kEventLeftButtonDown;
    up_msg   = kEventLeftButtonUp;
    dbl_msg  = kEventLeftDoubleClick;
    break;
  case BTN_RIGHT:
    down_msg = kEventRightButtonDown;
    up_msg   = kEventRightButtonUp;
    dbl_msg  = kEventRightDoubleClick;
    break;
  case BTN_MIDDLE:
    down_msg = kEventOtherButtonDown;
    up_msg   = kEventOtherButtonUp;
    dbl_msg  = kEventOtherDoubleClick;
    break;
  default:
    return;
  }

  if (pressed) {
    uint32_t msg = down_msg;
    if (events.last_btn == button && (time - events.last_btn_time) <= 300) {
      msg = dbl_msg;
      events.last_btn_time = 0; /* reset so triple-click doesn't double-fire */
    } else {
      events.last_btn_time = time;
      events.last_btn = button;
    }
    /* For double-click: also emit MouseDown first so that handlers which
     * only listen for MouseDown still process the second click. */
    if (msg == dbl_msg)
      events.queue[events.write++] = (EVENT){
        .x = (uint16_t)events.pointer.x,
        .y = (uint16_t)events.pointer.y,
        .message = down_msg,
      };
    events.queue[events.write++] = (EVENT){
      .x = (uint16_t)events.pointer.x,
      .y = (uint16_t)events.pointer.y,
      .message = msg,
    };
  } else {
    events.queue[events.write++] = (EVENT){
      .x = (uint16_t)events.pointer.x,
      .y = (uint16_t)events.pointer.y,
      .message = up_msg,
    };
  }
}

static void
pointer_axis(void* data,
             struct wl_pointer* pointer,
             uint32_t time,
             uint32_t axis,
             wl_fixed_t value)
{
  int16_t dx = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? 0 : -wl_fixed_to_double(value);
  int16_t dy = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? -wl_fixed_to_double(value) : 0;
  events.queue[events.write++] = (EVENT) {
    .x = events.pointer.x,
    .y = events.pointer.y,
    .lParam = (void*)(intptr_t)((dy)<<16|dx),
    .message = kEventScrollWheel
  };
}

// Attach pointer listener
static struct wl_pointer_listener pointer_listener = {
  .enter = pointer_enter,
  .leave = pointer_leave,
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis,
};

static void
keyboard_keymap(void* data,
                struct wl_keyboard* keyboard,
                uint32_t format,
                int fd,
                uint32_t size)
{
  close(fd);
}

static void
keyboard_enter(void* data,
               struct wl_keyboard* keyboard,
               uint32_t serial,
               struct wl_surface* surface,
               struct wl_array* keys)
{
  events.queue[events.write++] = (EVENT){ .message = kEventSetFocus };
}

static void
keyboard_leave(void* data,
               struct wl_keyboard* keyboard,
               uint32_t serial,
               struct wl_surface* surface)
{
  events.queue[events.write++] = (EVENT){ .message = kEventKillFocus };
}

static uint32_t
keysym_to_ax_key(xkb_keysym_t sym)
{
  /* Printable ASCII: normalise letters to lowercase */
  if (sym >= 0x20 && sym <= 0x7e)
    return (uint32_t)tolower((int)sym);

  switch (sym) {
  case XKB_KEY_Tab:       return AX_KEY_TAB;
  case XKB_KEY_Return:    return AX_KEY_ENTER;
  case XKB_KEY_Escape:    return AX_KEY_ESCAPE;
  case XKB_KEY_space:     return AX_KEY_SPACE;
  case XKB_KEY_BackSpace: return AX_KEY_BACKSPACE;

  case XKB_KEY_Up:        return AX_KEY_UPARROW;
  case XKB_KEY_Down:      return AX_KEY_DOWNARROW;
  case XKB_KEY_Left:      return AX_KEY_LEFTARROW;
  case XKB_KEY_Right:     return AX_KEY_RIGHTARROW;

  case XKB_KEY_Alt_L:
  case XKB_KEY_Alt_R:     return AX_KEY_ALT;
  case XKB_KEY_Control_L:
  case XKB_KEY_Control_R: return AX_KEY_CTRL;
  case XKB_KEY_Shift_L:
  case XKB_KEY_Shift_R:   return AX_KEY_SHIFT;
  case XKB_KEY_Super_L:
  case XKB_KEY_Super_R:   return AX_KEY_ALT; /* map Super to Alt for uniformity */

  case XKB_KEY_F1:        return AX_KEY_F1;
  case XKB_KEY_F2:        return AX_KEY_F2;
  case XKB_KEY_F3:        return AX_KEY_F3;
  case XKB_KEY_F4:        return AX_KEY_F4;
  case XKB_KEY_F5:        return AX_KEY_F5;
  case XKB_KEY_F6:        return AX_KEY_F6;
  case XKB_KEY_F7:        return AX_KEY_F7;
  case XKB_KEY_F8:        return AX_KEY_F8;
  case XKB_KEY_F9:        return AX_KEY_F9;
  case XKB_KEY_F10:       return AX_KEY_F10;
  case XKB_KEY_F11:       return AX_KEY_F11;
  case XKB_KEY_F12:       return AX_KEY_F12;

  case XKB_KEY_Insert:    return AX_KEY_INS;
  case XKB_KEY_Delete:    return AX_KEY_DEL;
  case XKB_KEY_Page_Down: return AX_KEY_PGDN;
  case XKB_KEY_Page_Up:   return AX_KEY_PGUP;
  case XKB_KEY_Home:      return AX_KEY_HOME;
  case XKB_KEY_End:       return AX_KEY_END;
  case XKB_KEY_Pause:     return AX_KEY_PAUSE;

  case XKB_KEY_KP_Home:     return AX_KEY_KP_HOME;
  case XKB_KEY_KP_Up:       return AX_KEY_KP_UPARROW;
  case XKB_KEY_KP_Page_Up:  return AX_KEY_KP_PGUP;
  case XKB_KEY_KP_Left:     return AX_KEY_KP_LEFTARROW;
  case XKB_KEY_KP_Begin:    return AX_KEY_KP_5;
  case XKB_KEY_KP_Right:    return AX_KEY_KP_RIGHTARROW;
  case XKB_KEY_KP_End:      return AX_KEY_KP_END;
  case XKB_KEY_KP_Down:     return AX_KEY_KP_DOWNARROW;
  case XKB_KEY_KP_Page_Down: return AX_KEY_KP_PGDN;
  case XKB_KEY_KP_Enter:    return AX_KEY_KP_ENTER;
  case XKB_KEY_KP_Insert:   return AX_KEY_KP_INS;
  case XKB_KEY_KP_Delete:   return AX_KEY_KP_DEL;
  case XKB_KEY_KP_Divide:   return AX_KEY_KP_SLASH;
  case XKB_KEY_KP_Subtract: return AX_KEY_KP_MINUS;
  case XKB_KEY_KP_Add:      return AX_KEY_KP_PLUS;

  default: return 0;
  }
}

static uint32_t
wayland_modifiers(void)
{
  uint32_t mods = 0;
  if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_SHIFT,
                                   XKB_STATE_MODS_EFFECTIVE) > 0)
    mods |= AX_MOD_SHIFT;
  if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL,
                                   XKB_STATE_MODS_EFFECTIVE) > 0)
    mods |= AX_MOD_CTRL;
  if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_ALT,
                                   XKB_STATE_MODS_EFFECTIVE) > 0)
    mods |= AX_MOD_ALT;
  if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_LOGO,
                                   XKB_STATE_MODS_EFFECTIVE) > 0)
    mods |= AX_MOD_CMD;
  return mods;
}

static void
keyboard_key(void* data,
             struct wl_keyboard* keyboard,
             uint32_t serial,
             uint32_t time,
             uint32_t key,
             uint32_t state)
{
  xkb_keycode_t keycode = key + 8;
  xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state, keycode);
  uint32_t axkey = keysym_to_ax_key(keysym);

  if (!axkey)
    return;

  uint32_t mods = wayland_modifiers();
  bool_t pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);
  uint32_t msg = pressed ? kEventKeyDown : kEventKeyUp;

  PEVENT e = &events.queue[events.write++];
  *e = (EVENT){
    .message = msg,
    .wParam  = axkey | mods,
  };

  /* For key-down, also store the UTF-8 character in lParam */
  if (pressed) {
    e->lParam = NULL;
    xkb_state_key_get_utf8(xkb_state, keycode, (char*)&e->lParam,
                           sizeof(e->lParam));
  }
}

static void
keyboard_modifiers(void* data,
                   struct wl_keyboard* keyboard,
                   uint32_t serial,
                   uint32_t mods_depressed,
                   uint32_t mods_latched,
                   uint32_t mods_locked,
                   uint32_t group)
{
  xkb_state_update_mask(xkb_state,
                        mods_depressed, mods_latched, mods_locked,
                        0, 0, group);
}

// Attach keyboard listener
static struct wl_keyboard_listener keyboard_listener = {
  .keymap = keyboard_keymap,
  .enter = keyboard_enter,
  .leave = keyboard_leave,
  .key = keyboard_key,
  .modifiers = keyboard_modifiers,
};

static void
seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities)
{
  if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
    pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(pointer, &pointer_listener, &events);
  }
  if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
    keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, &events);
  }
}

static struct wl_seat_listener seat_listener = {
  .capabilities = seat_capabilities,
};

struct wl_seat_listener*
get_seat_listener(void)
{
  return &seat_listener;
}

static void
xdg_surface_configure(void* data,
                      struct xdg_surface* xdg_surface,
                      uint32_t serial)
{
  xdg_surface_ack_configure(xdg_surface, serial);
  events.queue[events.write++] = (EVENT){ .message = kEventWindowPaint };
}

static struct xdg_surface_listener xdg_surface_listener = {
  xdg_surface_configure,
};

struct xdg_surface_listener*
get_xdg_surface_listener(void)
{
  return &xdg_surface_listener;
}

int
axWaitEvent(TIME time)
{
  extern struct wl_display* display;

  if (!display)
    return 0;

  if (time > 0) {
    /* Include the joystick fd and timerfd descriptors in the poll set. */
    struct pollfd fds[2 + MAX_TIMERS];
    int nfds = 0;
    fds[nfds].fd     = wl_display_get_fd(display);
    fds[nfds].events = POLLIN;
    nfds++;
    int joy_fd = joy_get_fd();
    if (joy_fd >= 0) {
      fds[nfds].fd     = joy_fd;
      fds[nfds].events = POLLIN;
      nfds++;
    }
    for (int i = 0; i < MAX_TIMERS; i++) {
      if (s_timers[i].id != 0) {
        fds[nfds].fd     = s_timers[i].fd;
        fds[nfds].events = POLLIN;
        nfds++;
      }
    }

    int ret = poll(fds, nfds, (int)time);
    if (ret > 0) {
      if (fds[0].revents & POLLIN) {
        wl_display_dispatch(display);
      }
      joy_poll();
      timers_poll();
      return 1;
    }
    return 0;
  }

  // No timeout, just dispatch pending events
  wl_display_dispatch_pending(display);
  joy_poll();
  timers_poll();
  return 0;
}

int
axPollEvent(PEVENT pEvent)
{
  joy_poll();
  timers_poll();
  if (events.read != events.write) {
    *pEvent = events.queue[events.read++];
    return 1;
  } else {
    return 0;
  }
}

void NotifyWindowEvent(void *window, uint32_t eventType, uint32_t wparam) {
  events.queue[events.write++] = (EVENT){ 
    .wParam = wparam,
    .message = kEventWindowPaint,
    .target = window,
  };
}

void
axPostMessageW(void* hobj, uint32_t event, uint32_t wparam, void* lparam)
{
  if (events.write - events.read >= sizeof(events.queue) / sizeof(events.queue[0])) {
    // Queue is full, ignore the message
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
axRemoveFromQueue(void* hobj)
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
axNotifyFileDropEvent(char const* filename, float x, float y)
{
  // File drop not fully implemented yet
  // Would need to allocate and copy filename string
  (void)filename;
  (void)x;
  (void)y;
}

uint32_t
axSetTimer(void* obj, uint32_t interval_ms, void* userdata, bool_t repeat)
{
  int slot = -1;
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id == 0) { slot = i; break; }
  if (slot < 0)
    return 0;
  int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (fd < 0)
    return 0;
  long sec  = (long)(interval_ms / 1000);
  long nsec = (long)(interval_ms % 1000) * 1000000L;
  struct itimerspec its = {
    .it_value    = { .tv_sec = sec, .tv_nsec = nsec },
    .it_interval = repeat ? (struct timespec){ .tv_sec = sec, .tv_nsec = nsec }
                          : (struct timespec){ .tv_sec = 0, .tv_nsec = 0 },
  };
  if (timerfd_settime(fd, 0, &its, NULL) < 0) {
    close(fd);
    return 0;
  }
  uint32_t tid          = s_next_timer_id++;
  s_timers[slot].id       = tid;
  s_timers[slot].fd       = fd;
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
      close(s_timers[i].fd);
      s_timers[i].id       = 0;
      s_timers[i].obj      = NULL;
      s_timers[i].userdata = NULL;
      s_timers[i].repeat   = FALSE;
      return;
    }
  }
}
