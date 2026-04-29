#include "../platform.h"
#include "x11_local.h"

#include <poll.h>
#ifdef __linux__
#include <sys/timerfd.h>
#endif
#include <unistd.h>

/* Defined in unix/unix_joystick.c */
extern void joy_poll(void);
extern int  joy_get_fd(void);

static struct
{
  EVENT queue[0x10000];
  WORD  write, read;
  float pointer_x, pointer_y;
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

/* --------------------------------------------------------------------------
 * XDND drag-and-drop state
 * -------------------------------------------------------------------------- */

static Atom xdnd_enter;
static Atom xdnd_position;
static Atom xdnd_status;
static Atom xdnd_leave;
static Atom xdnd_drop;
static Atom xdnd_finished;
static Atom xdnd_action_copy;
static Atom xdnd_selection;
static Atom xdnd_type_list;
static Atom xa_uri_list;

static Window xdnd_source = None;
static Time   xdnd_drop_time = CurrentTime;
static float  xdnd_x = 0, xdnd_y = 0;
static bool_t xdnd_has_uri = FALSE;

static void
x11_dnd_init(void)
{
  static bool_t done = FALSE;
  if (done || !x_display) return;
  done = TRUE;
  xdnd_enter       = XInternAtom(x_display, "XdndEnter",      False);
  xdnd_position    = XInternAtom(x_display, "XdndPosition",   False);
  xdnd_status      = XInternAtom(x_display, "XdndStatus",     False);
  xdnd_leave       = XInternAtom(x_display, "XdndLeave",      False);
  xdnd_drop        = XInternAtom(x_display, "XdndDrop",       False);
  xdnd_finished    = XInternAtom(x_display, "XdndFinished",   False);
  xdnd_action_copy = XInternAtom(x_display, "XdndActionCopy", False);
  xdnd_selection   = XInternAtom(x_display, "XdndSelection",  False);
  xdnd_type_list   = XInternAtom(x_display, "XdndTypeList",   False);
  xa_uri_list      = XInternAtom(x_display, "text/uri-list",  False);
}

/* Decode percent-encoded characters in a URI path component into dst. */
static void
uri_decode(char *dst, char const *src, size_t dstlen)
{
  size_t di = 0;
  while (*src && di + 1 < dstlen) {
    if (src[0] == '%') {
      int hi = -1, lo = -1;
      char c1 = src[1], c2 = src[2];
      if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
      else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
      else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
      if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
      else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
      else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
      if (hi >= 0 && lo >= 0) {
        dst[di++] = (char)((hi << 4) | lo);
        src += 3;
        continue;
      }
    }
    dst[di++] = *src++;
  }
  dst[di] = '\0';
}

/* Parse a text/uri-list payload and post kEventDragDrop for each file URI. */
static void
xdnd_process_uri_list(char const *data, float drop_x, float drop_y)
{
  char const *line = data;
  while (line && *line) {
    char const *end = strchr(line, '\n');
    /* length of this line (without \n) */
    size_t len = end ? (size_t)(end - line) : strlen(line);
    /* strip trailing \r */
    while (len > 0 && line[len - 1] == '\r') len--;
    /* skip blank lines and comments */
    if (len == 0 || line[0] == '#') {
      line = end ? end + 1 : NULL;
      continue;
    }
    /* only handle file:// URIs */
    if (len > 7 && strncmp(line, "file://", 7) == 0) {
      char const *path = line + 7;
      size_t path_len = len - 7;
      /* skip optional hostname: file://hostname/path → find first '/' */
      if (*path != '/') {
        char const *slash = memchr(path, '/', path_len);
        if (!slash) { line = end ? end + 1 : NULL; continue; }
        path_len -= (size_t)(slash - path);
        path = slash;
      }
      char raw[4096];
      if (path_len >= sizeof(raw)) path_len = sizeof(raw) - 1;
      memcpy(raw, path, path_len);
      raw[path_len] = '\0';
      char decoded[4096];
      uri_decode(decoded, raw, sizeof(decoded));
      axNotifyFileDropEvent(decoded, drop_x, drop_y);
    }
    line = end ? end + 1 : NULL;
  }
}

/* Handle XDND-related ClientMessage events. */
static void
xdnd_handle_client_message(XEvent const *xev)
{
  Atom msg = xev->xclient.message_type;

  if (msg == xdnd_enter) {
    xdnd_source = (Window)xev->xclient.data.l[0];
    bool_t has_more = (xev->xclient.data.l[1] & 1) != 0;
    xdnd_has_uri = FALSE;

    if (has_more) {
      /* More than 3 types: read XdndTypeList property from source. */
      Atom actual_type;
      int actual_format;
      unsigned long nitems, bytes_after;
      unsigned char *prop = NULL;
      XGetWindowProperty(x_display, xdnd_source, xdnd_type_list,
                         0, 65536, False, XA_ATOM,
                         &actual_type, &actual_format,
                         &nitems, &bytes_after, &prop);
      if (prop) {
        Atom const *types = (Atom const *)prop;
        for (unsigned long j = 0; j < nitems; j++) {
          if (types[j] == xa_uri_list) { xdnd_has_uri = TRUE; break; }
        }
        XFree(prop);
      }
    } else {
      for (int j = 2; j <= 4; j++) {
        if ((Atom)xev->xclient.data.l[j] == xa_uri_list) {
          xdnd_has_uri = TRUE;
          break;
        }
      }
    }

  } else if (msg == xdnd_position) {
    Window src = (Window)xev->xclient.data.l[0];
    int root_x = (int)((xev->xclient.data.l[2] >> 16) & 0xffff);
    int root_y = (int)(xev->xclient.data.l[2] & 0xffff);
    xdnd_drop_time = (Time)xev->xclient.data.l[3];

    /* Convert root coordinates to window-local coordinates. */
    Window child;
    int win_x = 0, win_y = 0;
    XTranslateCoordinates(x_display, DefaultRootWindow(x_display), x_window,
                          root_x, root_y, &win_x, &win_y, &child);
    xdnd_x = (float)win_x;
    xdnd_y = (float)win_y;

    /* Reply with XdndStatus – accept if we support text/uri-list. */
    XEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type                 = ClientMessage;
    reply.xclient.window       = src;
    reply.xclient.message_type = xdnd_status;
    reply.xclient.format       = 32;
    reply.xclient.data.l[0]   = (long)x_window;
    reply.xclient.data.l[1]   = xdnd_has_uri ? 1 : 0;
    reply.xclient.data.l[4]   = (long)(xdnd_has_uri ? xdnd_action_copy : 0);
    XSendEvent(x_display, src, False, NoEventMask, &reply);
    XFlush(x_display);

  } else if (msg == xdnd_leave) {
    xdnd_source = None;
    xdnd_has_uri = FALSE;

  } else if (msg == xdnd_drop) {
    if (xdnd_source == None || !xdnd_has_uri) {
      xdnd_source = None;
      return;
    }
    Time ts = (Time)xev->xclient.data.l[2];
    if (ts != CurrentTime) xdnd_drop_time = ts;
    /* Request the selection data as text/uri-list. */
    XConvertSelection(x_display, xdnd_selection, xa_uri_list,
                      xdnd_selection, x_window, xdnd_drop_time);
    XFlush(x_display);
  }
}

/* Check all timerfd descriptors for readability and post kEventTimer */
static void
timers_poll(void)
{
#ifdef __linux__
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
#endif
}

/* Map an X11 KeySym to a AX_KEY_* value */
static uint32_t
keysym_to_ax_key(KeySym sym)
{
  /* Printable ASCII: pass through directly */
  if (sym >= 0x20 && sym <= 0x7e) {
    return (uint32_t)sym;
  }

  switch (sym) {
  case XK_Tab:        return AX_KEY_TAB;
  case XK_Return:     return AX_KEY_ENTER;
  case XK_Escape:     return AX_KEY_ESCAPE;
  case XK_space:      return AX_KEY_SPACE;
  case XK_BackSpace:  return AX_KEY_BACKSPACE;

  case XK_Up:         return AX_KEY_UPARROW;
  case XK_Down:       return AX_KEY_DOWNARROW;
  case XK_Left:       return AX_KEY_LEFTARROW;
  case XK_Right:      return AX_KEY_RIGHTARROW;

  case XK_Alt_L:
  case XK_Alt_R:      return AX_KEY_ALT;
  case XK_Control_L:
  case XK_Control_R:  return AX_KEY_CTRL;
  case XK_Shift_L:
  case XK_Shift_R:    return AX_KEY_SHIFT;

  case XK_F1:         return AX_KEY_F1;
  case XK_F2:         return AX_KEY_F2;
  case XK_F3:         return AX_KEY_F3;
  case XK_F4:         return AX_KEY_F4;
  case XK_F5:         return AX_KEY_F5;
  case XK_F6:         return AX_KEY_F6;
  case XK_F7:         return AX_KEY_F7;
  case XK_F8:         return AX_KEY_F8;
  case XK_F9:         return AX_KEY_F9;
  case XK_F10:        return AX_KEY_F10;
  case XK_F11:        return AX_KEY_F11;
  case XK_F12:        return AX_KEY_F12;

  case XK_Insert:     return AX_KEY_INS;
  case XK_Delete:     return AX_KEY_DEL;
  case XK_Page_Down:  return AX_KEY_PGDN;
  case XK_Page_Up:    return AX_KEY_PGUP;
  case XK_Home:       return AX_KEY_HOME;
  case XK_End:        return AX_KEY_END;
  case XK_Pause:      return AX_KEY_PAUSE;

  case XK_KP_Home:    return AX_KEY_KP_HOME;
  case XK_KP_Up:      return AX_KEY_KP_UPARROW;
  case XK_KP_Page_Up: return AX_KEY_KP_PGUP;
  case XK_KP_Left:    return AX_KEY_KP_LEFTARROW;
  case XK_KP_Begin:   return AX_KEY_KP_5;
  case XK_KP_Right:   return AX_KEY_KP_RIGHTARROW;
  case XK_KP_End:     return AX_KEY_KP_END;
  case XK_KP_Down:    return AX_KEY_KP_DOWNARROW;
  case XK_KP_Page_Down: return AX_KEY_KP_PGDN;
  case XK_KP_Enter:   return AX_KEY_KP_ENTER;
  case XK_KP_Insert:  return AX_KEY_KP_INS;
  case XK_KP_Delete:  return AX_KEY_KP_DEL;
  case XK_KP_Divide:  return AX_KEY_KP_SLASH;
  case XK_KP_Subtract: return AX_KEY_KP_MINUS;
  case XK_KP_Add:     return AX_KEY_KP_PLUS;

  default:            return 0;
  }
}

/* Collect modifier flags from an X11 state mask */
static uint32_t
x11_modifiers(unsigned int state)
{
  uint32_t mods = 0;
  if (state & ShiftMask)   mods |= AX_MOD_SHIFT;
  if (state & ControlMask) mods |= AX_MOD_CTRL;
  if (state & Mod1Mask)    mods |= AX_MOD_ALT;  /* Alt */
  if (state & Mod4Mask)    mods |= AX_MOD_CMD;  /* Super/Meta */
  return mods;
}

/* Process all pending X11 events and push them onto the internal queue */
static void
x11_process_events(void)
{
  if (!x_display) {
    return;
  }

  x11_dnd_init();

  /* State for double-click detection */
  static uint32_t last_button = 0;
  static Time     last_time   = 0;
  static int      last_x      = 0;
  static int      last_y      = 0;
  /* Bitmask of currently held mouse buttons (bit N = button N) */
  static uint32_t buttons_held = 0;

  XEvent xev;
  while (XPending(x_display)) {
    XNextEvent(x_display, &xev);

    switch (xev.type) {

    case KeyPress:
    case KeyRelease: {
      KeySym sym = XLookupKeysym(&xev.xkey, 0);
      uint32_t key = keysym_to_ax_key(sym);
      if (key) {
        uint32_t mods = x11_modifiers(xev.xkey.state);
        EVENT *e = &events.queue[events.write++];
        e->target  = NULL;
        e->message = (xev.type == KeyPress) ? kEventKeyDown : kEventKeyUp;
        e->wParam  = key | mods;
        e->lParam  = NULL;
        /* Store the UTF-8 character for key-down events */
        if (xev.type == KeyPress) {
          XLookupString(&xev.xkey, (char*)&e->lParam,
                        (int)sizeof(e->lParam) - 1, NULL, NULL);
        }
      }
      break;
    }

    case ButtonPress:
    case ButtonRelease: {
      bool_t pressed = (xev.type == ButtonPress);
      uint32_t msg = 0;
      switch (xev.xbutton.button) {
      case Button1:
        if (pressed) buttons_held |=  (1u << Button1);
        else         buttons_held &= ~(1u << Button1);
        if (pressed) {
          /* Double-click detection */
          int dx = xev.xbutton.x - last_x;
          int dy = xev.xbutton.y - last_y;
          int dist2 = dx*dx + dy*dy;
          if (last_button == Button1 &&
              (xev.xbutton.time - last_time) <= 300 &&
              dist2 <= 25) {
            msg = kEventLeftDoubleClick;
            last_time = 0; /* reset so triple-click doesn't double-fire */
          } else {
            msg = kEventLeftButtonDown;
            last_time = xev.xbutton.time;
            last_button = Button1;
            last_x = xev.xbutton.x;
            last_y = xev.xbutton.y;
          }
        } else {
          msg = kEventLeftButtonUp;
        }
        break;
      case Button2:
        if (pressed) buttons_held |=  (1u << Button2);
        else         buttons_held &= ~(1u << Button2);
        if (pressed) {
          int dx = xev.xbutton.x - last_x;
          int dy = xev.xbutton.y - last_y;
          int dist2 = dx*dx + dy*dy;
          if (last_button == Button2 &&
              (xev.xbutton.time - last_time) <= 300 &&
              dist2 <= 25) {
            msg = kEventOtherDoubleClick;
            last_time = 0;
          } else {
            msg = kEventOtherButtonDown;
            last_time = xev.xbutton.time;
            last_button = Button2;
            last_x = xev.xbutton.x;
            last_y = xev.xbutton.y;
          }
        } else {
          msg = kEventOtherButtonUp;
        }
        break;
      case Button3:
        if (pressed) buttons_held |=  (1u << Button3);
        else         buttons_held &= ~(1u << Button3);
        if (pressed) {
          int dx = xev.xbutton.x - last_x;
          int dy = xev.xbutton.y - last_y;
          int dist2 = dx*dx + dy*dy;
          if (last_button == Button3 &&
              (xev.xbutton.time - last_time) <= 300 &&
              dist2 <= 25) {
            msg = kEventRightDoubleClick;
            last_time = 0;
          } else {
            msg = kEventRightButtonDown;
            last_time = xev.xbutton.time;
            last_button = Button3;
            last_x = xev.xbutton.x;
            last_y = xev.xbutton.y;
          }
        } else {
          msg = kEventRightButtonUp;
        }
        break;
      case Button4:
        if (pressed) {
          events.queue[events.write++] = (EVENT){
            .x = (uint16_t)xev.xbutton.x,
            .y = (uint16_t)xev.xbutton.y,
            .dy = -1,
            .message = kEventScrollWheel,
          };
        }
        break;
      case Button5:
        if (pressed) {
          events.queue[events.write++] = (EVENT){
            .x = (uint16_t)xev.xbutton.x,
            .y = (uint16_t)xev.xbutton.y,
            .dy = 1,
            .message = kEventScrollWheel,
          };
        }
        break;
      }
      if (msg) {
        /* For double-click events, also emit a preceding MouseDown so that
         * handlers which only listen for MouseDown still process the second
         * click (matches SDL and WinAPI behaviour). */
        uint32_t down_msg = 0;
        if      (msg == kEventLeftDoubleClick)  down_msg = kEventLeftButtonDown;
        else if (msg == kEventRightDoubleClick) down_msg = kEventRightButtonDown;
        else if (msg == kEventOtherDoubleClick) down_msg = kEventOtherButtonDown;
        if (down_msg)
          events.queue[events.write++] = (EVENT){
            .x = (uint16_t)xev.xbutton.x,
            .y = (uint16_t)xev.xbutton.y,
            .message = down_msg,
          };
        events.queue[events.write++] = (EVENT){
          .x = (uint16_t)xev.xbutton.x,
          .y = (uint16_t)xev.xbutton.y,
          .message = msg,
        };
      }
      break;
    }

    case MotionNotify: {
      int16_t dx = (int16_t)(xev.xmotion.x - (int)events.pointer_x);
      int16_t dy = (int16_t)(xev.xmotion.y - (int)events.pointer_y);
      events.pointer_x = (float)xev.xmotion.x;
      events.pointer_y = (float)xev.xmotion.y;

      uint32_t msg;
      if (buttons_held & (1u << Button1))
        msg = kEventLeftButtonDragged;
      else if (buttons_held & (1u << Button3))
        msg = kEventRightButtonDragged;
      else if (buttons_held & (1u << Button2))
        msg = kEventOtherButtonDragged;
      else
        msg = kEventMouseMoved;

      events.queue[events.write++] = (EVENT){
        .x = (uint16_t)xev.xmotion.x,
        .y = (uint16_t)xev.xmotion.y,
        .dx = dx,
        .dy = dy,
        .message = msg,
      };
      break;
    }

    case ConfigureNotify: {
      extern struct _WND window;
      if (xev.xconfigure.width != window.width ||
          xev.xconfigure.height != window.height) {
        window.width  = xev.xconfigure.width;
        window.height = xev.xconfigure.height;
        events.queue[events.write++] = (EVENT){
          .wParam  = MAKEDWORD(xev.xconfigure.width, xev.xconfigure.height),
          .message = kEventWindowResized,
        };
      }
      break;
    }

    case Expose:
      if (xev.xexpose.count == 0) {
        events.queue[events.write++] = (EVENT){
          .message = kEventWindowPaint,
        };
      }
      break;

    case FocusIn:
      events.queue[events.write++] = (EVENT){
        .message = kEventSetFocus,
      };
      break;

    case FocusOut:
      events.queue[events.write++] = (EVENT){
        .message = kEventKillFocus,
      };
      break;

    case ClientMessage:
      if ((Atom)xev.xclient.data.l[0] == wm_delete_window) {
        events.queue[events.write++] = (EVENT){
          .message = kEventWindowClosed,
        };
      } else {
        xdnd_handle_client_message(&xev);
      }
      break;

    case SelectionNotify: {
      if (xev.xselection.selection != xdnd_selection) break;
      if (xev.xselection.property == None) {
        /* Conversion failed – send XdndFinished with accepted=0 */
        if (xdnd_source != None) {
          XEvent reply;
          memset(&reply, 0, sizeof(reply));
          reply.type                 = ClientMessage;
          reply.xclient.window       = xdnd_source;
          reply.xclient.message_type = xdnd_finished;
          reply.xclient.format       = 32;
          reply.xclient.data.l[0]   = (long)x_window;
          reply.xclient.data.l[1]   = 0;
          XSendEvent(x_display, xdnd_source, False, NoEventMask, &reply);
          XFlush(x_display);
          xdnd_source = None;
        }
        break;
      }
      Atom actual_type;
      int actual_format;
      unsigned long nitems, bytes_after;
      unsigned char *prop = NULL;
      XGetWindowProperty(x_display, x_window, xev.xselection.property,
                         0, 65536, True, AnyPropertyType,
                         &actual_type, &actual_format,
                         &nitems, &bytes_after, &prop);
      if (prop && nitems > 0) {
        xdnd_process_uri_list((char const *)prop, xdnd_x, xdnd_y);
        XFree(prop);
      }
      /* Send XdndFinished */
      if (xdnd_source != None) {
        XEvent reply;
        memset(&reply, 0, sizeof(reply));
        reply.type                 = ClientMessage;
        reply.xclient.window       = xdnd_source;
        reply.xclient.message_type = xdnd_finished;
        reply.xclient.format       = 32;
        reply.xclient.data.l[0]   = (long)x_window;
        reply.xclient.data.l[1]   = (prop && nitems > 0) ? 1 : 0;
        reply.xclient.data.l[2]   = (long)xdnd_action_copy;
        XSendEvent(x_display, xdnd_source, False, NoEventMask, &reply);
        XFlush(x_display);
        xdnd_source = None;
      }
      break;
    }

    default:
      break;
    }
  }
}

/* Build a poll set that includes the X11 connection fd, optionally the
 * joystick fd, and any active timerfd descriptors.
 * fds must have room for at least 2 + MAX_TIMERS entries. */
static int
x11_build_poll_fds(struct pollfd fds[2 + MAX_TIMERS])
{
  int nfds = 0;
  fds[nfds].fd     = ConnectionNumber(x_display);
  fds[nfds].events = POLLIN;
  nfds++;
  int joy_fd = joy_get_fd();
  if (joy_fd >= 0) {
    fds[nfds].fd     = joy_fd;
    fds[nfds].events = POLLIN;
    nfds++;
  }
#ifdef __linux__
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id != 0) {
      fds[nfds].fd     = s_timers[i].fd;
      fds[nfds].events = POLLIN;
      nfds++;
    }
  }
#endif
  return nfds;
}

int
axWaitMessage(TIME timeout_ms)
{
  if (!x_display) {
    return 0;
  }

  if (XPending(x_display)) {
    x11_process_events();
    return 1;
  }

  if (timeout_ms > 0) {
    /* Include the joystick fd and timerfd descriptors in the poll set. */
    struct pollfd fds[2 + MAX_TIMERS];
    int nfds = x11_build_poll_fds(fds);
    int ret = poll(fds, nfds, (int)timeout_ms);
    if (ret > 0) {
      x11_process_events();
      joy_poll();
      timers_poll();
      return 1;
    }
    return 0;
  }

  /* Block indefinitely, but wake on joystick and timer input too. */
  for (;;) {
    struct pollfd fds[2 + MAX_TIMERS];
    int nfds = x11_build_poll_fds(fds);
    int ret = poll(fds, nfds, 16 /* ms */);
    if (ret > 0) {
      x11_process_events();
      joy_poll();
      timers_poll();
      return 1;
    }
    /* Timed out with no input — poll joystick anyway, then loop. */
    joy_poll();
  }
}

int
axPeekMessage(PEVENT pEvent)
{
  joy_poll();
  x11_process_events();
  timers_poll();

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
      return 0;
    }
    if (axPeekMessage(pEvent)) {
      return 1;
    }
  }
}

void
NotifyWindowEvent(void* window, uint32_t eventType, uint32_t wparam)
{
  events.queue[events.write++] = (EVENT){
    .wParam   = wparam,
    .message  = eventType,
    .target   = window,
  };
}

void
axPostMessageW(void* hobj, uint32_t event, uint32_t wparam, void* lparam)
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
axRemoveFromQueue(void* hobj)
{
  WORD read_idx  = events.read;
  WORD write_idx = events.write;
  WORD new_write = events.read;

  while (read_idx != write_idx) {
    if (events.queue[read_idx].target != hobj)
      events.queue[new_write++] = events.queue[read_idx];
    read_idx++;
  }
  events.write = new_write;
#ifdef __linux__
  for (int i = 0; i < MAX_TIMERS; i++)
    if (s_timers[i].id != 0 && s_timers[i].obj == hobj)
      axCancelTimer(s_timers[i].id);
#endif
}

void
axNotifyFileDropEvent(char const* filename, float x, float y)
{
  if (!filename || !filename[0]) return;
  char *path = strdup(filename);
  if (!path) return;
  axPostMessageW(NULL, kEventDragDrop, MAKEDWORD((uint16_t)x, (uint16_t)y), path);
}

uint32_t
axSetTimer(void* obj, uint32_t interval_ms, void* userdata, bool_t repeat)
{
#ifdef __linux__
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
#else
  (void)obj; (void)interval_ms; (void)userdata; (void)repeat;
  return 0;
#endif
}

void
axCancelTimer(uint32_t timer_id)
{
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].id == timer_id) {
#ifdef __linux__
      close(s_timers[i].fd);
#endif
      s_timers[i].id       = 0;
      s_timers[i].obj      = NULL;
      s_timers[i].userdata = NULL;
      s_timers[i].repeat   = FALSE;
      return;
    }
  }
}
