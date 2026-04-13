/*
 * unix_joystick.c – Linux joystick support via /dev/input/js*.
 *
 * This file is compiled on both Linux and macOS (the macOS Makefile includes
 * unix/.c).  All Linux-specific code is guarded by #ifdef __linux__ so that
 * the file compiles to nothing on macOS, where macos_joystick.m provides the
 * joystick API stubs instead.
 *
 * On Linux, events are read from the first available joystick device in
 * non-blocking mode.  joy_poll() should be called from axPollEvent() and
 * axWaitEvent() in the platform backend (wayland_event.c / x11_event.c) so
 * that joystick events are mixed into the same queue as keyboard/mouse events.
 * joy_get_fd() exposes the open device fd so it can be added to poll() sets in
 * axWaitEvent(), allowing joystick activity to wake an otherwise-blocked loop.
 *
 * Encoding in AX_Message:
 *   kEventJoyAxisMotion : wParam = axis index, lParam = (void*)(intptr_t)value
 *   kEventJoyButtonDown / kEventJoyButtonUp : wParam = button index
 */

#ifdef __linux__

#include "../platform.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>

static int  g_joy_fd   = -1;
static char g_joy_name[128] = { 0 };

bool_t
axJoystickInit(void)
{
  char path[32];
  for (int i = 0; i < 8; i++) {
    snprintf(path, sizeof(path), "/dev/input/js%d", i);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
      g_joy_fd = fd;
      g_joy_name[0] = '\0';
      if (ioctl(fd, JSIOCGNAME(sizeof(g_joy_name) - 1), g_joy_name) < 0 ||
          g_joy_name[0] == '\0') {
        snprintf(g_joy_name, sizeof(g_joy_name), "Joystick %d", i);
      }
      return TRUE;
    }
  }
  return FALSE;
}

void
axJoystickShutdown(void)
{
  if (g_joy_fd >= 0) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
}

bool_t
axJoystickAvailable(void)
{
  return g_joy_fd >= 0 ? TRUE : FALSE;
}

char const *
axJoystickGetName(void)
{
  return g_joy_fd >= 0 ? g_joy_name : NULL;
}

/* Return the raw joystick file descriptor so callers can include it in
 * poll() sets (e.g. AX_WaitEvent).  Returns -1 when no device is open. */
int
joy_get_fd(void)
{
  return g_joy_fd;
}

/* Called from axPollEvent() and axWaitEvent() in each Linux backend to
 * drain pending joystick events.  Detects device disconnection via errno. */
void
joy_poll(void)
{
  if (g_joy_fd < 0) {
    return;
  }

  struct js_event je;
  ssize_t n;
  while ((n = read(g_joy_fd, &je, sizeof(je))) == (ssize_t)sizeof(je)) {
    uint8_t type = je.type & ~(uint8_t)JS_EVENT_INIT;
    if (type == JS_EVENT_AXIS) {
      axPostMessageW(NULL, kEventJoyAxisMotion,
                      (uint32_t)je.number,
                      (void *)(intptr_t)je.value);
    } else if (type == JS_EVENT_BUTTON) {
      uint32_t msg = je.value ? kEventJoyButtonDown : kEventJoyButtonUp;
      axPostMessageW(NULL, msg, (uint32_t)je.number, NULL);
    }
  }

  /* A hard error (not EAGAIN/EWOULDBLOCK) means the device is gone. */
  if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
}

#endif /* __linux__ */
