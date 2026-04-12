#include "../platform.h"
#include "webgl_local.h"

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_ctx = 0;
int g_canvas_width = 0;
int g_canvas_height = 0;

bool_t
axCreateWindow(PCSTR title, DWORD width, DWORD height, DWORD flags)
{
  (void)title;
  (void)width;
  (void)height;
  /* Most AX_WINDOW_* flags have no meaningful equivalent in a browser canvas.
   * AX_WINDOW_FULLSCREEN could be requested via the Fullscreen API but
   * requires a user gesture; it is left to the caller's JS code.
   * AX_WINDOW_HIGHDPI is always active (handled by axInit / axGetScaling). */
  (void)flags;

  /* If AX_Init already sized the canvas, skip the fallback resize */
  if (g_canvas_width == 0 || g_canvas_height == 0) {
    double css_width, css_height;
    emscripten_get_element_css_size("#canvas", &css_width, &css_height);
    g_canvas_width  = (int)css_width;
    g_canvas_height = (int)css_height;
    emscripten_set_canvas_element_size("#canvas",
      (int)(g_canvas_width  * axGetScaling() + 0.5),
      (int)(g_canvas_height * axGetScaling() + 0.5));
  }

  emscripten_webgl_make_context_current(g_webgl_ctx);
  printf("Created window with size %dx%d\n", g_canvas_width, g_canvas_height);
  axPostMessageW(NULL, kEventWindowPaint, MAKEDWORD(g_canvas_width, g_canvas_height), NULL);
  return TRUE;
}

uint32_t
axGetSize(struct AXsize *size)
{
  if (size) {
    size->width = (uint32_t)g_canvas_width;
    size->height = (uint32_t)g_canvas_height;
  }
  return MAKEDWORD(g_canvas_width, g_canvas_height);
}

bool_t
axSetSize(uint32_t width, uint32_t height, bool_t centered)
{
  (void)centered;
  g_canvas_width = (int)width;
  g_canvas_height = (int)height;
  emscripten_set_canvas_element_size("#canvas", 
    (int)(width * axGetScaling() + 0.5), 
    (int)(height * axGetScaling() + 0.5));
  axPostMessageW(NULL, kEventWindowResized, MAKEDWORD(width, height), NULL);
  return TRUE;
}

#include <math.h>
float
axGetScaling(void)
{
  return fmaxf((float)emscripten_get_device_pixel_ratio(), 1.0f);
}

void
axMakeCurrentContext(void)
{
  emscripten_webgl_make_context_current(g_webgl_ctx);
}

void
axBeginPaint(void)
{
  emscripten_webgl_make_context_current(g_webgl_ctx);
}

void
axEndPaint(void)
{
  // WebGL swapping is handled by the browser automatically
}

void
axBindFramebuffer(void)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool_t
axCreateSurface(uint32_t width, uint32_t height)
{
  (void)width;
  (void)height;
  return FALSE;
}
