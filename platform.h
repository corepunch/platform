#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#ifndef MAX
#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#endif

#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

// Platform API export macro
#ifndef ORCA_API
#define ORCA_API
#endif

// Basic types
typedef int bool_t;
typedef const char* lpcString_t;
typedef unsigned long longTime_t;

enum
{
  K_TAB = 9,
  K_ENTER = 13,
  K_ESCAPE = 27,
  K_SPACE = 32,
  K_BACKSPACE = 127,
  K_UPARROW = 128,
  K_DOWNARROW = 129,
  K_LEFTARROW = 130,
  K_RIGHTARROW = 131,
  K_ALT = 132,
  K_CTRL = 133,
  K_SHIFT = 134,
  K_F1 = 135,
  K_F2 = 136,
  K_F3 = 137,
  K_F4 = 138,
  K_F5 = 139,
  K_F6 = 140,
  K_F7 = 141,
  K_F8 = 142,
  K_F9 = 143,
  K_F10 = 144,
  K_F11 = 145,
  K_F12 = 146,
  K_INS = 147,
  K_DEL = 148,
  K_PGDN = 149,
  K_PGUP = 150,
  K_HOME = 151,
  K_END = 152,
  K_KP_HOME = 160,
  K_KP_UPARROW = 161,
  K_KP_PGUP = 162,
  K_KP_LEFTARROW = 163,
  K_KP_5 = 164,
  K_KP_RIGHTARROW = 165,
  K_KP_END = 166,
  K_KP_DOWNARROW = 167,
  K_KP_PGDN = 168,
  K_KP_ENTER = 169,
  K_KP_INS = 170,
  K_KP_DEL = 171,
  K_KP_SLASH = 172,
  K_KP_MINUS = 173,
  K_KP_PLUS = 174,
  K_PAUSE = 255,
  K_MOUSE1 = 200,
  K_MOUSE2 = 201,
  K_MOUSE3 = 202,
  K_JOY1 = 203,
  K_JOY2 = 204,
  K_JOY3 = 205,
  K_JOY4 = 206,
  K_AUX1 = 207,
  K_AUX2 = 208,
  K_AUX3 = 209,
  K_AUX4 = 210,
  K_AUX5 = 211,
  K_AUX6 = 212,
  K_AUX7 = 213,
  K_AUX8 = 214,
  K_AUX9 = 215,
  K_AUX10 = 216,
  K_AUX11 = 217,
  K_AUX12 = 218,
  K_AUX13 = 219,
  K_AUX14 = 220,
  K_AUX15 = 221,
  K_AUX16 = 222,
  K_AUX17 = 223,
  K_AUX18 = 224,
  K_AUX19 = 225,
  K_AUX20 = 226,
  K_AUX21 = 227,
  K_AUX22 = 228,
  K_AUX23 = 229,
  K_AUX24 = 230,
  K_AUX25 = 231,
  K_AUX26 = 232,
  K_AUX27 = 233,
  K_AUX28 = 234,
  K_AUX29 = 235,
  K_AUX30 = 236,
  K_AUX31 = 237,
  K_AUX32 = 238,
  K_MWHEELDOWN = 239,
  K_MWHEELUP = 240,
};

enum
{
  MOD_SHIFT = 1 << 16,
  MOD_CTRL = 1 << 17,
  MOD_ALT = 1 << 18,
  MOD_CMD = 1 << 19,
};

typedef enum _EVTMOUSEBTN
{
  BUT_LEFT,
  BUT_RIGHT,
  BUT_MIDDLE,
  BUT_UNKNOWN,
  BUT_COUNT,
} EVTMOUSEBTN;

// Structure declarations for platform-specific implementations
struct buffer {
  uint8_t* data;
  size_t cursize;
  size_t maxsize;
};

struct message {
  uint32_t message;
  uint32_t wParam;
  void* lParam;
  void* hobj;
  float x, y;
};

struct isize2 {
  int width;
  int height;
};

struct vec2 {
  float x;
  float y;
};

// Function declarations that may be implemented externally
void Con_Error(const char* fmt, ...);
void* ZeroAlloc(size_t size);

#endif
