#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "events.h"

#ifndef LOWORD
#define LOWORD(l) ((uint16_t)(l & 0xFFFF))
#endif

#ifndef HIWORD
#define HIWORD(l) ((uint16_t)((l >> 16) & 0xFFFF))
#endif

#ifndef MAKEDWORD
#define MAKEDWORD(low, high) ((uint32_t)(((uint16_t)(low)) | ((uint32_t)((uint16_t)(high))) << 16))
#endif

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
#ifndef WI_API
#define WI_API __attribute__((visibility("default")))
#endif

// Basic types
typedef unsigned int bool_t;
typedef unsigned long longTime_t;
typedef uint32_t wParam_t;
typedef void* lParam_t;
typedef unsigned char byte_t;

enum
{
  WI_KEY_TAB = 9,
  WI_KEY_ENTER = 13,
  WI_KEY_ESCAPE = 27,
  WI_KEY_SPACE = 32,
  WI_KEY_BACKSPACE = 127,
  WI_KEY_UPARROW = 128,
  WI_KEY_DOWNARROW = 129,
  WI_KEY_LEFTARROW = 130,
  WI_KEY_RIGHTARROW = 131,
  WI_KEY_ALT = 132,
  WI_KEY_CTRL = 133,
  WI_KEY_SHIFT = 134,
  WI_KEY_F1 = 135,
  WI_KEY_F2 = 136,
  WI_KEY_F3 = 137,
  WI_KEY_F4 = 138,
  WI_KEY_F5 = 139,
  WI_KEY_F6 = 140,
  WI_KEY_F7 = 141,
  WI_KEY_F8 = 142,
  WI_KEY_F9 = 143,
  WI_KEY_F10 = 144,
  WI_KEY_F11 = 145,
  WI_KEY_F12 = 146,
  WI_KEY_INS = 147,
  WI_KEY_DEL = 148,
  WI_KEY_PGDN = 149,
  WI_KEY_PGUP = 150,
  WI_KEY_HOME = 151,
  WI_KEY_END = 152,
  WI_KEY_KP_HOME = 160,
  WI_KEY_KP_UPARROW = 161,
  WI_KEY_KP_PGUP = 162,
  WI_KEY_KP_LEFTARROW = 163,
  WI_KEY_KP_5 = 164,
  WI_KEY_KP_RIGHTARROW = 165,
  WI_KEY_KP_END = 166,
  WI_KEY_KP_DOWNARROW = 167,
  WI_KEY_KP_PGDN = 168,
  WI_KEY_KP_ENTER = 169,
  WI_KEY_KP_INS = 170,
  WI_KEY_KP_DEL = 171,
  WI_KEY_KP_SLASH = 172,
  WI_KEY_KP_MINUS = 173,
  WI_KEY_KP_PLUS = 174,
  WI_KEY_PAUSE = 255,
  WI_KEY_MOUSE1 = 200,
  WI_KEY_MOUSE2 = 201,
  WI_KEY_MOUSE3 = 202,
  WI_KEY_JOY1 = 203,
  WI_KEY_JOY2 = 204,
  WI_KEY_JOY3 = 205,
  WI_KEY_JOY4 = 206,
  WI_KEY_AUX1 = 207,
  WI_KEY_AUX2 = 208,
  WI_KEY_AUX3 = 209,
  WI_KEY_AUX4 = 210,
  WI_KEY_AUX5 = 211,
  WI_KEY_AUX6 = 212,
  WI_KEY_AUX7 = 213,
  WI_KEY_AUX8 = 214,
  WI_KEY_AUX9 = 215,
  WI_KEY_AUX10 = 216,
  WI_KEY_AUX11 = 217,
  WI_KEY_AUX12 = 218,
  WI_KEY_AUX13 = 219,
  WI_KEY_AUX14 = 220,
  WI_KEY_AUX15 = 221,
  WI_KEY_AUX16 = 222,
  WI_KEY_AUX17 = 223,
  WI_KEY_AUX18 = 224,
  WI_KEY_AUX19 = 225,
  WI_KEY_AUX20 = 226,
  WI_KEY_AUX21 = 227,
  WI_KEY_AUX22 = 228,
  WI_KEY_AUX23 = 229,
  WI_KEY_AUX24 = 230,
  WI_KEY_AUX25 = 231,
  WI_KEY_AUX26 = 232,
  WI_KEY_AUX27 = 233,
  WI_KEY_AUX28 = 234,
  WI_KEY_AUX29 = 235,
  WI_KEY_AUX30 = 236,
  WI_KEY_AUX31 = 237,
  WI_KEY_AUX32 = 238,
  WI_KEY_MWHEELDOWN = 239,
  WI_KEY_MWHEELUP = 240,
};

enum
{
  WI_MOD_SHIFT = 1 << 16,
  WI_MOD_CTRL = 1 << 17,
  WI_MOD_ALT = 1 << 18,
  WI_MOD_CMD = 1 << 19,
};

struct WI_Buffer
{
  byte_t* data;
  int maxsize;
  int cursize;
  int readcount;
};

struct WI_Message
{
  void* target;
  uint32_t message;
  union {
    wParam_t wParam;
    struct { uint16_t x, y; };
    struct { uint16_t keyCode, modflags; };
  };
  union {
    lParam_t lParam;
    struct { int16_t dx, dy; };
  };
  uint32_t id;
};

struct WI_Size
{
  uint32_t width, height;
};

WI_API void
WI_PostMessageW(void* hobj, uint32_t event, uint32_t wparam, void* lparam);

WI_API int
WI_PollEvent(struct WI_Message*);

WI_API void
WI_RemoveFromQueue(void*);

WI_API void
NotifyFileDropEvent(char const *filename, float x, float y);

enum {
  OFN_FILEMUSTEXIST = 1 << 0,
  OFN_PATHMUSTEXIST = 1 << 1,
};

typedef struct _WI_OpenFileName {
  char *lpstrFile;
  uint32_t nMaxFile;
  char const *lpstrFilter;
  char const *lpstrTitle;
  uint32_t Flags;
} WI_OpenFileName;

WI_API bool_t
WI_GetOpenFileName(WI_OpenFileName const *);

WI_API bool_t
WI_GetSaveFileName(WI_OpenFileName const *);

WI_API bool_t
WI_GetFolderName(WI_OpenFileName const *);

WI_API void
WI_Init(void);

WI_API void
WI_Shutdown(void);

WI_API longTime_t
WI_GetMilliseconds(void);

WI_API void
WI_Sleep(longTime_t msec);

WI_API bool_t
WI_IsDarkTheme(void);

WI_API char const *
WI_GetPlatform(void);

WI_API char const *
WI_SettingsDirectory(void);

WI_API char const *
WI_ShareDirectory(void);

WI_API char const *
WI_LibDirectory(void);

/*
 Window operations
 */

WI_API bool_t
WI_CreateWindow(char const *, uint32_t width, uint32_t height, uint32_t flags);

WI_API bool_t
WI_CreateSurface(uint32_t width, uint32_t height);

WI_API float
WI_GetScaling(void);

WI_API void
WI_Shutdown(void);

WI_API bool_t
WI_SetSize(uint32_t width, uint32_t height, bool_t centered);

WI_API uint32_t
WI_GetSize(struct WI_Size*);

WI_API int
WI_WaitEvent(longTime_t);

WI_API void
WI_MakeCurrentContext(void);

WI_API void
WI_BeginPaint(void);

WI_API void
WI_EndPaint(void);

WI_API void
WI_BindFramebuffer(void);

WI_API char const *
WI_KeynumToString(uint32_t keynum);


#endif
