/*
 * windows_joystick.c – Joystick support and swap interval for Windows.
 *
 * Joystick input is implemented using XInput, which supports Xbox-compatible
 * controllers.  State is polled each time WI_PollEvent() is called and
 * compared against the previous snapshot; changes generate kEventJoyAxisMotion
 * and kEventJoyButton* events that are pushed to the event queue.
 *
 * Swap interval uses wglSwapIntervalEXT when available.
 *
 * Encoding in WI_Message:
 *   kEventJoyAxisMotion : wParam = axis index, lParam = (void*)(intptr_t)value
 *   kEventJoyButtonDown / kEventJoyButtonUp : wParam = button index
 */

#include "windows_local.h"
#include "../platform.h"

#include <xinput.h>

static BOOL g_joy_available = FALSE;
static DWORD g_joy_user_index = 0;

static XINPUT_STATE g_prev_state;
static BOOL         g_has_prev = FALSE;

/* Axis indices exposed through the API */
enum {
  JOY_AXIS_LX = 0,
  JOY_AXIS_LY = 1,
  JOY_AXIS_RX = 2,
  JOY_AXIS_RY = 3,
  JOY_AXIS_LT = 4,
  JOY_AXIS_RT = 5,
};

/* XInput button → sequential index mapping */
static const struct { WORD bit; uint32_t idx; } k_buttons[] = {
  { XINPUT_GAMEPAD_DPAD_UP,        0  },
  { XINPUT_GAMEPAD_DPAD_DOWN,      1  },
  { XINPUT_GAMEPAD_DPAD_LEFT,      2  },
  { XINPUT_GAMEPAD_DPAD_RIGHT,     3  },
  { XINPUT_GAMEPAD_START,          4  },
  { XINPUT_GAMEPAD_BACK,           5  },
  { XINPUT_GAMEPAD_LEFT_THUMB,     6  },
  { XINPUT_GAMEPAD_RIGHT_THUMB,    7  },
  { XINPUT_GAMEPAD_LEFT_SHOULDER,  8  },
  { XINPUT_GAMEPAD_RIGHT_SHOULDER, 9  },
  { XINPUT_GAMEPAD_A,              10 },
  { XINPUT_GAMEPAD_B,              11 },
  { XINPUT_GAMEPAD_X,              12 },
  { XINPUT_GAMEPAD_Y,              13 },
};
#define NUM_BUTTONS ((int)(sizeof(k_buttons)/sizeof(k_buttons[0])))

bool_t
WI_JoystickInit(void)
{
  for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(state));
    if (XInputGetState(i, &state) == ERROR_SUCCESS) {
      g_joy_user_index = i;
      g_joy_available  = TRUE;
      g_prev_state     = state;
      g_has_prev       = TRUE;
      return TRUE;
    }
  }
  return FALSE;
}

void
WI_JoystickShutdown(void)
{
  g_joy_available = FALSE;
  g_has_prev      = FALSE;
}

bool_t
WI_JoystickAvailable(void)
{
  return g_joy_available ? TRUE : FALSE;
}

char const *
WI_JoystickGetName(void)
{
  return g_joy_available ? "XInput Controller" : NULL;
}

/* Called from WI_PollEvent() in windows_event.c */
void
joy_poll(void)
{
  if (!g_joy_available) {
    return;
  }

  XINPUT_STATE cur;
  ZeroMemory(&cur, sizeof(cur));
  if (XInputGetState(g_joy_user_index, &cur) != ERROR_SUCCESS) {
    g_joy_available = FALSE;
    return;
  }

  if (!g_has_prev) {
    g_prev_state = cur;
    g_has_prev   = TRUE;
    return;
  }

  XINPUT_GAMEPAD *p = &g_prev_state.Gamepad;
  XINPUT_GAMEPAD *c = &cur.Gamepad;

  /* --- Axis events ---
   * Triggers (bLeftTrigger / bRightTrigger) are 8-bit unsigned [0, 255].
   * Duplicate the byte into both halves of an int16_t so that the range
   * maps to roughly [0, 65535] and the full 16-bit field can be compared
   * and reported consistently with the thumb-stick axes. */
  struct axis_check { int16_t prev; int16_t cur; uint32_t idx; };
  struct axis_check ax[] = {
    { p->sThumbLX,  c->sThumbLX,  JOY_AXIS_LX },
    { p->sThumbLY,  c->sThumbLY,  JOY_AXIS_LY },
    { p->sThumbRX,  c->sThumbRX,  JOY_AXIS_RX },
    { p->sThumbRY,  c->sThumbRY,  JOY_AXIS_RY },
    { (int16_t)((p->bLeftTrigger  << 8) | p->bLeftTrigger),
      (int16_t)((c->bLeftTrigger  << 8) | c->bLeftTrigger),
      JOY_AXIS_LT },
    { (int16_t)((p->bRightTrigger << 8) | p->bRightTrigger),
      (int16_t)((c->bRightTrigger << 8) | c->bRightTrigger),
      JOY_AXIS_RT },
  };

  for (int i = 0; i < 6; i++) {
    if (ax[i].prev != ax[i].cur) {
      WI_PostMessageW(NULL, kEventJoyAxisMotion, ax[i].idx,
                      (void *)(intptr_t)ax[i].cur);
    }
  }

  /* --- Button events --- */
  for (int i = 0; i < NUM_BUTTONS; i++) {
    BOOL was_down = (p->wButtons & k_buttons[i].bit) != 0;
    BOOL is_down  = (c->wButtons & k_buttons[i].bit) != 0;
    if (was_down != is_down) {
      uint32_t msg = is_down ? kEventJoyButtonDown : kEventJoyButtonUp;
      WI_PostMessageW(NULL, msg, k_buttons[i].idx, NULL);
    }
  }

  g_prev_state = cur;
}

/* --- Swap interval ---------------------------------------------------- */

typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int interval);

bool_t
WI_SetSwapInterval(int interval)
{
  static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;
  static BOOL loaded = FALSE;

  if (!loaded) {
    loaded = TRUE;
    wglSwapIntervalEXT =
      (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
  }

  if (!wglSwapIntervalEXT) {
    return FALSE;
  }

  return wglSwapIntervalEXT(interval) ? TRUE : FALSE;
}
