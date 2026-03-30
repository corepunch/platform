/*
 * webgl_joystick.c – Joystick stubs and swap interval for WebGL.
 *
 * The HTML5 Gamepad API is event-driven and callback-based.  Swap interval
 * is handled by the browser and cannot be set programmatically in WebGL.
 * Both features therefore degrade gracefully by returning FALSE / NULL.
 *
 * A more complete implementation could poll navigator.getGamepads() from
 * a JS interop call and translate changes into platform events.
 */

#include "../platform.h"
#include "webgl_local.h"

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
  /* The browser controls VSync; EMSCRIPTEN_SET_MAIN_LOOP_TIMING can affect
   * the target frame rate but not the hardware swap interval directly. */
  (void)interval;
  return FALSE;
}
