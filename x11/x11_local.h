#ifndef __X11_LOCAL__
#define __X11_LOCAL__

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ZeroAlloc(size) calloc(1, size)

typedef struct _WND
{
  EGLContext  egl_context;
  EGLSurface  egl_surface;
  int         width;
  int         height;
  struct _WND* next;
}* PWND;

struct vec2 {
  float x;
  float y;
};

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef char const *PCSTR;
typedef char PATHSTR[1024];
typedef struct AXsize *PSIZE2;
typedef struct AXsize const *PCSIZE2;
typedef struct AXmessage EVENT, *PEVENT;
typedef struct vec2 VECTOR2D;
typedef unsigned long TIME;

extern EGLDisplay egl_display;
extern Display*   x_display;
extern Window     x_window;
extern Atom       wm_delete_window;

#endif
