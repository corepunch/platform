/*
 * unix_joystick.c – Linux joystick support via /dev/input/js*.
 *
 * Events are read from the first available joystick device in non-blocking
 * mode.  joy_poll() should be called from WI_PollEvent() in the platform
 * backend (wayland_event.c / x11_event.c) so that joystick events are mixed
 * into the same queue as keyboard and mouse events.
 *
 * Encoding in WI_Message:
 *   kEventJoyAxisMotion : wParam = axis index, lParam = (void*)(intptr_t)value
 *   kEventJoyButtonDown / kEventJoyButtonUp : wParam = button index
 */

#include "../platform.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>

static int  g_joy_fd   = -1;
static char g_joy_name[128] = { 0 };

bool_t
WI_JoystickInit(void)
{
  char path[32];
  for (int i = 0; i < 8; i++) {
    snprintf(path, sizeof(path), "/dev/input/js%d", i);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
      g_joy_fd = fd;
      g_joy_name[0] = '\0';
      ioctl(fd, JSIOCGNAME(sizeof(g_joy_name) - 1), g_joy_name);
      if (g_joy_name[0] == '\0') {
        snprintf(g_joy_name, sizeof(g_joy_name), "Joystick %d", i);
      }
      return TRUE;
    }
  }
  return FALSE;
}

void
WI_JoystickShutdown(void)
{
  if (g_joy_fd >= 0) {
    close(g_joy_fd);
    g_joy_fd = -1;
  }
}

bool_t
WI_JoystickAvailable(void)
{
  return g_joy_fd >= 0 ? TRUE : FALSE;
}

char const *
WI_JoystickGetName(void)
{
  return g_joy_fd >= 0 ? g_joy_name : NULL;
}

/* Called from WI_PollEvent() in each Linux backend to drain joystick events. */
void
joy_poll(void)
{
  if (g_joy_fd < 0) {
    return;
  }

  struct js_event je;
  while (read(g_joy_fd, &je, sizeof(je)) == (ssize_t)sizeof(je)) {
    uint8_t type = je.type & ~(uint8_t)JS_EVENT_INIT;
    if (type == JS_EVENT_AXIS) {
      WI_PostMessageW(NULL, kEventJoyAxisMotion,
                      (uint32_t)je.number,
                      (void *)(intptr_t)je.value);
    } else if (type == JS_EVENT_BUTTON) {
      uint32_t msg = je.value ? kEventJoyButtonDown : kEventJoyButtonUp;
      WI_PostMessageW(NULL, msg, (uint32_t)je.number, NULL);
    }
  }
}
