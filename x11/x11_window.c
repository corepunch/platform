#include "x11_local.h"
#include "../platform.h"
#include <X11/Xatom.h>

/* Motif window-manager hints structure (subset used to control decorations). */
typedef struct {
  unsigned long flags;
  unsigned long functions;
  unsigned long decorations;
  long          inputMode;
  unsigned long status;
} MWMHints;

bool_t
WI_CreateWindow(char const* title, uint32_t width, uint32_t height, uint32_t flags)
{
  extern struct _WND window;

  if (!x_display || !x_window) {
    return FALSE;
  }

  XStoreName(x_display, x_window, title);

  if (width > 0 && height > 0 &&
      (width != (uint32_t)window.width || height != (uint32_t)window.height)) {
    XResizeWindow(x_display, x_window, width, height);
    window.width  = (int)width;
    window.height = (int)height;
  }

  /* Fullscreen via _NET_WM_STATE */
  if (flags & WI_WINDOW_FULLSCREEN) {
    Atom wm_state   = XInternAtom(x_display, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(x_display, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type                 = ClientMessage;
    xev.xclient.window       = x_window;
    xev.xclient.message_type = wm_state;
    xev.xclient.format       = 32;
    xev.xclient.data.l[0]   = 1; /* _NET_WM_STATE_ADD */
    xev.xclient.data.l[1]   = (long)fullscreen;
    xev.xclient.data.l[2]   = 0;
    XSendEvent(x_display, DefaultRootWindow(x_display), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
  }

  /* Remove window decorations via Motif WM hints */
  if (flags & WI_WINDOW_BORDERLESS) {
    Atom mwm_atom = XInternAtom(x_display, "_MOTIF_WM_HINTS", False);
    MWMHints hints = { 2, 0, 0, 0, 0 }; /* flags=MWM_HINTS_DECORATIONS, decorations=0 */
    XChangeProperty(x_display, x_window, mwm_atom, mwm_atom, 32,
                    PropModeReplace, (unsigned char*)&hints, 5);
  }

  /* Lock size to prevent user resizing only when RESIZABLE is not set.
   * Note: since windows are resizable by default, RESIZABLE merely documents
   * the intent.  In a future API version a WI_WINDOW_FIXED flag could be
   * added to explicitly disable resize. */
  if (!(flags & WI_WINDOW_RESIZABLE) && (flags != 0) && width > 0 && height > 0) {
    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
      hints->flags      = PMinSize | PMaxSize;
      hints->min_width  = hints->max_width  = (int)width;
      hints->min_height = hints->max_height = (int)height;
      XSetWMNormalHints(x_display, x_window, hints);
      XFree(hints);
    }
  }

  /* Hide the window if requested */
  if (flags & WI_WINDOW_HIDDEN) {
    XUnmapWindow(x_display, x_window);
  }

  XFlush(x_display);
  WI_PostMessageW(NULL, kEventWindowPaint, 0, NULL);
  return TRUE;
}

void
WI_MakeCurrentContext(void)
{
  extern struct _WND window;
  eglMakeCurrent(egl_display, window.egl_surface,
                 window.egl_surface, window.egl_context);
}

void
WI_BeginPaint(void)
{
  WI_MakeCurrentContext();
}

void
WI_EndPaint(void)
{
  extern struct _WND window;
  glColorMask(0, 0, 0, 1);
  glClearColor(1, 1, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColorMask(1, 1, 1, 1);
  eglSwapBuffers(egl_display, window.egl_surface);
}

void
WI_BindFramebuffer(void)
{
  /* EGL uses the default framebuffer; nothing to bind */
}

bool_t
WI_CreateSurface(uint32_t width, uint32_t height)
{
  /* Off-screen surfaces are not implemented */
  (void)width;
  (void)height;
  return FALSE;
}

bool_t
WI_SetSwapInterval(int interval)
{
  EGLBoolean ok = eglSwapInterval(egl_display, interval);
  return ok == EGL_TRUE ? TRUE : FALSE;
}

float
WI_GetScaling(void)
{
  return 1.0f;
}

uint32_t
WI_GetSize(struct WI_Size* size)
{
  extern struct _WND window;
  if (size) {
    size->width  = (uint32_t)window.width;
    size->height = (uint32_t)window.height;
  }
  return MAKEDWORD(window.width, window.height);
}

bool_t
WI_SetSize(uint32_t width, uint32_t height, bool_t centered)
{
  extern struct _WND window;
  (void)centered;

  if (!x_display || !x_window) {
    return FALSE;
  }

  XResizeWindow(x_display, x_window, width, height);
  window.width  = (int)width;
  window.height = (int)height;
  XFlush(x_display);
  return TRUE;
}
