#include "wayland_local.h"
#include <include/orca.h>
#include <include/renderer.h>

static struct _WND* windows = NULL;

// static void create_window (struct _WND *window, int32_t width, int32_t
// height) { 	eglBindAPI (EGL_OPENGL_API); 	EGLint attributes[] = {
// EGL_RED_SIZE, 8, 		EGL_GREEN_SIZE, 8, 		EGL_BLUE_SIZE,
// 8, 	    EGL_NONE
//     };
// 	EGLConfig config;
// 	EGLint num_config;
// 	eglChooseConfig (egl_display, attributes, &config, 1, &num_config);
// 	window->egl_context = eglCreateContext (egl_display, config,
// EGL_NO_CONTEXT, NULL); 	window->surface = wl_compositor_create_surface
// (compositor); 	window->shell_surface = wl_shell_get_shell_surface
// (shell, window->surface); 	wl_shell_surface_add_listener
// (window->shell_surface, &shell_surface_listener, window);
// wl_shell_surface_set_toplevel (window->shell_surface);
// window->egl_window = wl_egl_window_create (window->surface, width, height);
// window->egl_surface = eglCreateWindowSurface (egl_display, config,
// window->egl_window, NULL); 	eglMakeCurrent (egl_display,
// window->egl_surface, window->egl_surface, window->egl_context);
// }

void
ResizeWindow(PWND win, DWORD width, DWORD height)
{
  extern struct _WND window;
  *win = window;
  wl_egl_window_resize(win->egl_window, width, height, -100, -100);
  win->width = width;
  win->height = height;
}

PWND
SYS_CreateWindow(PCSTR name, DWORD width, DWORD height, DWORD flags)
{
  PWND self = ZeroAlloc(sizeof(struct _WND));
  ResizeWindow(self, width, height);
  self->next = windows;
  windows = self;
  return self;
}

void
GetWindowSize(PWND self, PSIZE2 pSize)
{
  int width, height;
  wl_egl_window_get_attached_size(self->egl_window, &width, &height);
  pSize->width = self->width;
  pSize->height = self->height;
}

void
DestroyWindow(PWND hwnd)
{
  PWND* current = &windows;
  PWND target = hwnd;

  if (!target)
    return;

  // Remove from linked list
  while (*current) {
    if (*current == target) {
      *current = target->next; // Remove from list
      break;
    }
    current = &((*current)->next);
  }

  // Destroy EGL resources
  if (target->egl_window) {
    wl_egl_window_destroy(target->egl_window);
  }

  // Free memory
  free(target);
}

void
SYS_MakeCurrentContext(PWND self)
{
  eglMakeCurrent (egl_display, self->egl_surface, self->egl_surface, self->egl_context);
}


void
BeginPaint(PWND self)
{
  SYS_MakeCurrentContext(self);
  wl_display_dispatch(display);
}

void
EndPaint(PWND self)
{
  glColorMask(0,0,0,1);
  glClearColor(1,1,1,1);
  glClear(GL_COLOR_BUFFER_BIT);
  glColorMask(1,1,1,1);
  eglSwapBuffers(egl_display, self->egl_surface);
  wl_surface_commit(self->surface);
  wl_display_flush(display);
  wl_display_dispatch_pending(display);
}

float
GetWindowScale(PWND hWnd)
{
  return 1;
}

void
GetWindowPosition(PWND win, PSIZE2 pSize)
{
  if (!win || !pSize)
    return;

  pSize->width = win->width;
  pSize->height = win->height;
}

void
SetWindowPosition(PWND win, PCSIZE2 pSize)
{
  if (!win || !pSize)
    return;

  ResizeWindow(win, pSize->width, pSize->height);
}
