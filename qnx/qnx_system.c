#include <EGL/egl.h>
#include <screen/screen.h>
#include <sys/keycodes.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "qnx_local.h"

extern EGLDisplay egl_display;
extern EGLContext egl_ctx;
extern EGLSurface egl_surface;
extern screen_context_t screen_ctx;

extern void  BeginPaint(HWND hWnd);
extern void  EndPaint(HWND hWnd, LPMETRICS lpMetrics);
extern float GetWindowScale(HWND hWnd);
extern void  GetWindowSize(HWND hWnd, LPSIZE2 lpSize);

/*
 * Event queue with blocking wait support
 */

static struct
{
  struct AXmessage data[0x10000];
  uint16_t read, write;
} queue = { 0 };

static sem_t event_sem;

/* Screen event handle (created on first use) */
static screen_event_t screen_ev = NULL;

/* Previous pointer position and button state for drag/delta tracking */
static int qnx_ptr_x = 0;
static int qnx_ptr_y = 0;
static int qnx_buttons = 0;

/* Map a QNX key symbol to a AX_KEY_* constant */
static uint32_t
qnx_keysym_to_ax(int sym)
{
  /* Printable ASCII: normalise letters to lowercase */
  if (sym >= 0x20 && sym <= 0x7e)
    return (uint32_t)tolower(sym);

  switch (sym) {
  case KEYCODE_TAB:        return AX_KEY_TAB;
  case KEYCODE_RETURN:     return AX_KEY_ENTER;
  case KEYCODE_ESCAPE:     return AX_KEY_ESCAPE;
  case KEYCODE_SPACE:      return AX_KEY_SPACE;
  case KEYCODE_BACKSPACE:  return AX_KEY_BACKSPACE;

  case KEYCODE_UP:         return AX_KEY_UPARROW;
  case KEYCODE_DOWN:       return AX_KEY_DOWNARROW;
  case KEYCODE_LEFT:       return AX_KEY_LEFTARROW;
  case KEYCODE_RIGHT:      return AX_KEY_RIGHTARROW;

  case KEYCODE_LEFT_ALT:
  case KEYCODE_RIGHT_ALT:  return AX_KEY_ALT;
  case KEYCODE_LEFT_CTRL:
  case KEYCODE_RIGHT_CTRL: return AX_KEY_CTRL;
  case KEYCODE_LEFT_SHIFT:
  case KEYCODE_RIGHT_SHIFT: return AX_KEY_SHIFT;

  case KEYCODE_F1:         return AX_KEY_F1;
  case KEYCODE_F2:         return AX_KEY_F2;
  case KEYCODE_F3:         return AX_KEY_F3;
  case KEYCODE_F4:         return AX_KEY_F4;
  case KEYCODE_F5:         return AX_KEY_F5;
  case KEYCODE_F6:         return AX_KEY_F6;
  case KEYCODE_F7:         return AX_KEY_F7;
  case KEYCODE_F8:         return AX_KEY_F8;
  case KEYCODE_F9:         return AX_KEY_F9;
  case KEYCODE_F10:        return AX_KEY_F10;
  case KEYCODE_F11:        return AX_KEY_F11;
  case KEYCODE_F12:        return AX_KEY_F12;

  case KEYCODE_INSERT:     return AX_KEY_INS;
  case KEYCODE_DELETE:     return AX_KEY_DEL;
  case KEYCODE_PG_UP:      return AX_KEY_PGUP;
  case KEYCODE_PG_DOWN:    return AX_KEY_PGDN;
  case KEYCODE_HOME:       return AX_KEY_HOME;
  case KEYCODE_END:        return AX_KEY_END;
  case KEYCODE_PAUSE:      return AX_KEY_PAUSE;

  case KEYCODE_KP_HOME:    return AX_KEY_KP_HOME;
  case KEYCODE_KP_UP:      return AX_KEY_KP_UPARROW;
  case KEYCODE_KP_PG_UP:   return AX_KEY_KP_PGUP;
  case KEYCODE_KP_LEFT:    return AX_KEY_KP_LEFTARROW;
  case KEYCODE_KP_FIVE:    return AX_KEY_KP_5;
  case KEYCODE_KP_RIGHT:   return AX_KEY_KP_RIGHTARROW;
  case KEYCODE_KP_END:     return AX_KEY_KP_END;
  case KEYCODE_KP_DOWN:    return AX_KEY_KP_DOWNARROW;
  case KEYCODE_KP_PG_DOWN: return AX_KEY_KP_PGDN;
  case KEYCODE_KP_ENTER:   return AX_KEY_KP_ENTER;
  case KEYCODE_KP_INSERT:  return AX_KEY_KP_INS;
  case KEYCODE_KP_DELETE:  return AX_KEY_KP_DEL;
  case KEYCODE_KP_DIVIDE:  return AX_KEY_KP_SLASH;
  case KEYCODE_KP_MINUS:   return AX_KEY_KP_MINUS;
  case KEYCODE_KP_PLUS:    return AX_KEY_KP_PLUS;

  default: return 0;
  }
}

/* Collect AX modifier flags from a QNX key modifier bitmask */
static uint32_t
qnx_key_modifiers(int mods)
{
  uint32_t AX_MODs = 0;
  if (mods & KEYMOD_SHIFT)   AX_MODs |= AX_MOD_SHIFT;
  if (mods & KEYMOD_CTRL)    AX_MODs |= AX_MOD_CTRL;
  if (mods & KEYMOD_ALT)     AX_MODs |= AX_MOD_ALT;
  return AX_MODs;
}

/* Drain pending QNX Screen events into the AX queue */
static void
qnx_process_screen_events(void)
{
  if (!screen_ctx)
    return;

  if (!screen_ev) {
    if (screen_create_event(&screen_ev) != 0) {
      screen_ev = NULL;
      return;
    }
  }

  /* Double-click state */
  static uint32_t last_btn = 0;
  static uint64_t last_btn_ns = 0;
  static int      last_btn_x = 0;
  static int      last_btn_y = 0;

  while (screen_get_event(screen_ctx, screen_ev, 0) == 0) {
    int type = SCREEN_EVENT_NONE;
    screen_get_event_property_iv(screen_ev, SCREEN_PROPERTY_TYPE, &type);

    if (type == SCREEN_EVENT_NONE)
      break;

    switch (type) {

    case SCREEN_EVENT_POINTER: {
      int buttons = 0;
      int pos[2] = { 0, 0 };
      screen_get_event_property_iv(screen_ev, SCREEN_PROPERTY_BUTTONS,         &buttons);
      screen_get_event_property_iv(screen_ev, SCREEN_PROPERTY_SOURCE_POSITION, pos);

      int16_t dx = (int16_t)(pos[0] - qnx_ptr_x);
      int16_t dy = (int16_t)(pos[1] - qnx_ptr_y);
      int prev_buttons = qnx_buttons;
      qnx_ptr_x  = pos[0];
      qnx_ptr_y  = pos[1];
      qnx_buttons = buttons;

      /* Detect button press/release transitions */
      int pressed  = buttons  & ~prev_buttons;
      int released = prev_buttons & ~buttons;

      /* Scroll wheel: QNX uses bit flags SCREEN_MOUSE_BUTTON_SCROLL_UP/DOWN */
      if (buttons & SCREEN_MOUSE_BUTTON_SCROLL_UP) {
        struct AXmessage msg = {
          .message = kEventScrollWheel,
          .x = (uint16_t)pos[0],
          .y = (uint16_t)pos[1],
          .dy = -1,
        };
        queue.data[queue.write++] = msg;
        sem_post(&event_sem);
        break;
      }
      if (buttons & SCREEN_MOUSE_BUTTON_SCROLL_DOWN) {
        struct AXmessage msg = {
          .message = kEventScrollWheel,
          .x = (uint16_t)pos[0],
          .y = (uint16_t)pos[1],
          .dy = 1,
        };
        queue.data[queue.write++] = msg;
        sem_post(&event_sem);
        break;
      }

      /* Button press/release events */
      struct { int mask; uint32_t down; uint32_t up; uint32_t dbl; } btns[] = {
        { SCREEN_LEFT_MOUSE_BUTTON,   kEventLeftButtonDown,  kEventLeftButtonUp,  kEventLeftDoubleClick  },
        { SCREEN_RIGHT_MOUSE_BUTTON,  kEventRightButtonDown, kEventRightButtonUp, kEventRightDoubleClick },
        { SCREEN_MIDDLE_MOUSE_BUTTON, kEventOtherButtonDown, kEventOtherButtonUp, kEventOtherDoubleClick },
      };

      for (int i = 0; i < 3; i++) {
        if (pressed & btns[i].mask) {
          struct timespec ts;
          clock_gettime(CLOCK_MONOTONIC, &ts);
          uint64_t now_ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

          int bx = pos[0] - last_btn_x;
          int by = pos[1] - last_btn_y;
          uint32_t msg_type = btns[i].down;
          if (last_btn == (uint32_t)btns[i].mask &&
              (now_ns - last_btn_ns) <= 300000000ULL &&
              (bx*bx + by*by) <= 25) {
            msg_type = btns[i].dbl;
            last_btn_ns = 0;
          } else {
            last_btn = btns[i].mask;
            last_btn_ns = now_ns;
            last_btn_x = pos[0];
            last_btn_y = pos[1];
          }
          /* For double-click: also emit MouseDown first so that handlers
           * which only listen for MouseDown still process the second click. */
          if (msg_type == btns[i].dbl) {
            struct AXmessage down_ev = {
              .message = btns[i].down,
              .x = (uint16_t)pos[0],
              .y = (uint16_t)pos[1],
            };
            queue.data[queue.write++] = down_ev;
            sem_post(&event_sem);
          }
          struct AXmessage ev = {
            .message = msg_type,
            .x = (uint16_t)pos[0],
            .y = (uint16_t)pos[1],
          };
          queue.data[queue.write++] = ev;
          sem_post(&event_sem);
        }
        if (released & btns[i].mask) {
          struct AXmessage ev = {
            .message = btns[i].up,
            .x = (uint16_t)pos[0],
            .y = (uint16_t)pos[1],
          };
          queue.data[queue.write++] = ev;
          sem_post(&event_sem);
        }
      }

      /* Mouse motion */
      {
        uint32_t move_msg;
        if (buttons & SCREEN_LEFT_MOUSE_BUTTON)
          move_msg = kEventLeftButtonDragged;
        else if (buttons & SCREEN_RIGHT_MOUSE_BUTTON)
          move_msg = kEventRightButtonDragged;
        else if (buttons & SCREEN_MIDDLE_MOUSE_BUTTON)
          move_msg = kEventOtherButtonDragged;
        else
          move_msg = kEventMouseMoved;

        struct AXmessage ev = {
          .message = move_msg,
          .x = (uint16_t)pos[0],
          .y = (uint16_t)pos[1],
          .dx = dx,
          .dy = dy,
        };
        queue.data[queue.write++] = ev;
        sem_post(&event_sem);
      }
      break;
    }

    case SCREEN_EVENT_KEYBOARD: {
      int flags = 0, sym = 0, mods = 0;
      screen_get_event_property_iv(screen_ev, SCREEN_PROPERTY_KEY_FLAGS,     &flags);
      screen_get_event_property_iv(screen_ev, SCREEN_PROPERTY_KEY_SYM,       &sym);
      screen_get_event_property_iv(screen_ev, SCREEN_PROPERTY_KEY_MODIFIERS, &mods);

      uint32_t axkey = qnx_keysym_to_ax(sym);
      if (axkey) {
        uint32_t AX_MODs = qnx_key_modifiers(mods);
        bool_t pressed = (flags & KEY_DOWN) != 0;
        struct AXmessage ev = {
          .message = pressed ? kEventKeyDown : kEventKeyUp,
          .wParam  = axkey | AX_MODs,
        };
        /* Store printable character in lParam */
        if (pressed && sym >= 0x20 && sym <= 0x7e) {
          char ch = (char)sym;
          memcpy(&ev.lParam, &ch, 1);
        }
        queue.data[queue.write++] = ev;
        sem_post(&event_sem);
      }
      break;
    }

    case SCREEN_EVENT_CLOSE: {
      struct AXmessage ev = { .message = kEventWindowClosed };
      queue.data[queue.write++] = ev;
      sem_post(&event_sem);
      break;
    }

    default:
      break;
    }
  }
}

static void
init_event_sem(void)
{
  static int initialized = 0;
  if (!initialized) {
    sem_init(&event_sem, 0, 0);
    initialized = 1;
  }
}

void
axPostMessageW(void *hobj, uint32_t event, uint32_t wparam, void *lparam)
{
  init_event_sem();

  /* Coalesce duplicate resize/paint events */
  for (uint16_t r = queue.read; r != queue.write; r++) {
    if (queue.data[r].message != event)
      continue;
    switch (event) {
      case kEventWindowResized:
      case kEventWindowPaint:
        queue.data[r].target = hobj;
        queue.data[r].wParam = wparam;
        queue.data[r].lParam = lparam;
        return;
    }
  }

  queue.data[queue.write].target  = hobj;
  queue.data[queue.write].message = event;
  queue.data[queue.write].wParam  = wparam;
  queue.data[queue.write].lParam  = lparam;
  queue.write++;

  sem_post(&event_sem);
}

int
axPeekMessage(struct AXmessage *e)
{
  qnx_process_screen_events();
  if (queue.read == queue.write)
    return 0;
  *e = queue.data[queue.read++];
  return 1;
}

void
axRemoveFromQueue(void *target)
{
  for (uint16_t r = queue.read; r != queue.write; r++)
    if (queue.data[r].target == target)
      memset(&queue.data[r], 0, sizeof(queue.data[r]));
}

int
axWaitMessage(longTime_t msec)
{
  init_event_sem();

  if (msec > 0) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += msec / 1000;
    ts.tv_nsec += (msec % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
      ts.tv_sec++;
      ts.tv_nsec -= 1000000000;
    }
    return sem_timedwait(&event_sem, &ts) == 0 ? 1 : 0;
  }

  sem_wait(&event_sem);
  return 1;
}

int
axGetMessage(struct AXmessage *e)
{
  if (axPeekMessage(e)) {
    return 1;
  }

  for (;;) {
    if (axWaitMessage(0) <= 0) {
      continue;
    }
    if (axPeekMessage(e)) {
      return 1;
    }
  }
}

void
axNotifyFileDropEvent(char const *filename, float x, float y)
{
  (void)filename;
  (void)x;
  (void)y;
}

/*
 * File dialogs (not supported on QNX)
 */

bool_t
axGetOpenFileName(AXopenfilename const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
axGetSaveFileName(AXopenfilename const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
axGetFolderName(AXopenfilename const *ofn)
{
  (void)ofn;
  return FALSE;
}

/*
 * Timing
 */

longTime_t
axGetMilliseconds(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (longTime_t)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

void
axSleep(longTime_t msec)
{
  struct timespec ts;
  ts.tv_sec  = msec / 1000;
  ts.tv_nsec = (msec % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

/*
 * Theme / system info
 */

bool_t
axIsDarkTheme(void)
{
  return FALSE;
}

/*
 * Window operations
 */

bool_t
axCreateSurface(uint32_t width, uint32_t height)
{
  (void)width;
  (void)height;
  return FALSE;
}

float
axGetScaling(void)
{
  return GetWindowScale(NULL);
}

bool_t
axSetSize(uint32_t width, uint32_t height, bool_t centered)
{
  (void)centered;
  /* Actual screen/EGL window resize is not yet implemented on QNX.
   * Notify the application so it can respond to the size change. */
  axPostMessageW(NULL, kEventWindowResized, MAKEDWORD(width, height), NULL);
  return TRUE;
}

uint32_t
axGetSize(struct AXsize *pSize)
{
  SIZE2 size = { 0, 0 };
  GetWindowSize(NULL, &size);
  if (pSize) {
    pSize->width  = (uint32_t)size.width;
    pSize->height = (uint32_t)size.height;
  }
  return MAKEDWORD(size.width, size.height);
}

void
axMakeCurrentContext(void)
{
  BeginPaint(NULL);
}

void
axBeginPaint(void)
{
  BeginPaint(NULL);
}

void
axEndPaint(void)
{
  EndPaint(NULL, NULL);
}

void
axBindFramebuffer(void)
{
  /* EGL uses the default framebuffer (0) */
}

/*
 * Key names
 */

char const *
axKeynumToString(uint32_t keynum)
{
  static char tinystr[2];

  if (keynum == (uint32_t)-1)
    return "<KEY NOT FOUND>";

  keynum = keynum & 0xff;

  if (keynum > 32 && keynum < 127) {
    tinystr[0] = (char)keynum;
    tinystr[1] = 0;
    return tinystr;
  }

  return "<UNKNOWN KEYNUM>";
}

void *
axDynlibOpen(char const *path)
{
  return dlopen(path, RTLD_LAZY);
}

void *
axDynlibSym(void *handle, char const *sym)
{
  return dlsym(handle, sym);
}

void
axDynlibClose(void *handle)
{
  if (handle)
    dlclose(handle);
}

char const *
axDynlibError(void)
{
  return dlerror();
}

bool_t
axMkDir(char const *path)
{
  if (mkdir(path, 0777) == 0)
    return TRUE;
  
  if (errno == EEXIST) {
    /* Verify it's actually a directory, not a file */
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
      return TRUE;
  }
  
  return FALSE;
}

bool_t
axListDir(char const *path, AXDirCallback cb, void *userdata)
{
  DIR *dir = opendir(path);
  if (!dir)
    return FALSE;

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
      continue;

    char full[1024];
    int n = snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
    if (n < 0 || (size_t)n >= sizeof(full))
      continue; /* path too long, skip entry */

    struct stat st;
    if (stat(full, &st) != 0)
      continue;

    AXdirent entry;
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.name, ent->d_name, sizeof(entry.name) - 1);
    entry.is_directory = S_ISDIR(st.st_mode) ? TRUE : FALSE;
    entry.is_hidden    = (ent->d_name[0] == '.') ? TRUE : FALSE;
    entry.size         = entry.is_directory ? 0 : (size_t)st.st_size;
    entry.modified     = st.st_mtime;

    if (!cb(&entry, userdata))
      break;
  }

  closedir(dir);
  return TRUE;
}

bool_t
axPathExists(char const *path)
{
  struct stat st;
  return (path && stat(path, &st) == 0) ? TRUE : FALSE;
}

bool_t
axGetCwd(char *buf, size_t sz)
{
  return getcwd(buf, sz) != NULL ? TRUE : FALSE;
}
