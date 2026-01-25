#ifndef __WAYLAND_LOCAL__
#define __WAYLAND_LOCAL__

#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

#include <EGL/egl.h>
#include <GL/gl.h>
#include <string.h>

#include "xdg-shell-client.h"

typedef struct _WND
{
  EGLContext egl_context;
  EGLSurface egl_surface;
  struct wl_surface* surface;
  struct xdg_surface* xdg_surface;
  struct xdg_toplevel* xdg_toplevel;
  struct wl_egl_window* egl_window;
  struct _WND* next;
  int width;
  int height;
}* PWND;

typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef char const *PCSTR;
typedef char PATHSTR[1024];
typedef struct isize2 *PSIZE2;
typedef struct isize2 const *PCSIZE2;
typedef struct message EVENT, *PEVENT;
typedef struct vec2 VECTOR2D;
typedef unsigned long TIME;

extern EGLDisplay egl_display;
extern struct wl_display* display;
extern struct wl_compositor* compositor;
extern struct wl_shell* shell;
extern struct wl_shell_surface_listener shell_surface_listener;

#endif
