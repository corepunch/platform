/*
 * macos_joystick.m – Joystick stubs and swap interval for macOS.
 *
 * Full joystick support would require the IOKit HID framework.  For now the
 * joystick management functions return FALSE / NULL, which is a valid graceful-
 * degradation path (the application simply receives no joystick events).
 *
 * Swap interval is controlled via CGLSetParameter on the CGL context that
 * backs the NSOpenGLContext used by the window.
 */

#include "macos_local.h"

#import <OpenGL/OpenGL.h>

/* --------------------------------------------------------------------- */
/* Joystick (stub)                                                        */
/* --------------------------------------------------------------------- */

bool_t
axJoystickInit(void)
{
  return FALSE;
}

void
axJoystickShutdown(void)
{
}

bool_t
axJoystickAvailable(void)
{
  return FALSE;
}

char const *
axJoystickGetName(void)
{
  return NULL;
}

/* --------------------------------------------------------------------- */
/* Swap interval                                                          */
/* --------------------------------------------------------------------- */

bool_t
axSetSwapInterval(int interval)
{
  CGLContextObj ctx = CGLGetCurrentContext();
  if (!ctx) {
    return FALSE;
  }
  GLint swapInt = (GLint)interval;
  CGLError err = CGLSetParameter(ctx, kCGLCPSwapInterval, &swapInt);
  return (err == kCGLNoError) ? TRUE : FALSE;
}
