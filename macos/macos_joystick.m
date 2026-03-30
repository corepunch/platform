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
WI_JoystickInit(void)
{
  return FALSE;
}

void
WI_JoystickShutdown(void)
{
}

bool_t
WI_JoystickAvailable(void)
{
  return FALSE;
}

char const *
WI_JoystickGetName(void)
{
  return NULL;
}

/* --------------------------------------------------------------------- */
/* Swap interval                                                          */
/* --------------------------------------------------------------------- */

bool_t
WI_SetSwapInterval(int interval)
{
  CGLContextObj ctx = CGLGetCurrentContext();
  if (!ctx) {
    return FALSE;
  }
  GLint swapInt = (GLint)interval;
  CGLError err = CGLSetParameter(ctx, kCGLCPSwapInterval, &swapInt);
  return (err == kCGLNoError) ? TRUE : FALSE;
}
