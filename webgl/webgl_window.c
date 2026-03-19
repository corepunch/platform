#include "../platform.h"
#include "webgl_local.h"

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE g_webgl_ctx = 0;
int g_canvas_width = 640;
int g_canvas_height = 480;

bool_t
WI_CreateWindow(PCSTR title, DWORD width, DWORD height, DWORD flags)
{
  (void)title;
  (void)flags;
  (void)width;
  (void)height;

  /* Set canvas size to the actual browser inner window size at creation */
  double css_width, css_height;
  emscripten_get_element_css_size("#canvas", &css_width, &css_height);

  double dpr = emscripten_get_device_pixel_ratio();
  g_canvas_width  = (int)(css_width  * dpr + 0.5);
  g_canvas_height = (int)(css_height * dpr + 0.5);

  emscripten_set_canvas_element_size("#canvas", g_canvas_width, g_canvas_height);
  emscripten_webgl_make_context_current(g_webgl_ctx);
  printf("Created window with size %dx%d\n", g_canvas_width, g_canvas_height);
  WI_PostMessageW(NULL, kEventWindowPaint, MAKEDWORD(g_canvas_width, g_canvas_height), NULL);
  return TRUE;
}

uint32_t
WI_GetSize(struct WI_Size *size)
{
  if (size) {
    size->width = (uint32_t)g_canvas_width;
    size->height = (uint32_t)g_canvas_height;
  }
  return MAKEDWORD(g_canvas_width, g_canvas_height);
}

bool_t
WI_SetSize(uint32_t width, uint32_t height, bool_t centered)
{
  (void)centered;
  g_canvas_width = (int)width;
  g_canvas_height = (int)height;
  emscripten_set_canvas_element_size("#canvas", (int)width, (int)height);
  WI_PostMessageW(NULL, kEventWindowResized, MAKEDWORD(width, height), NULL);
  // WI_PostMessageW(NULL, kEventWindowPaint, MAKEDWORD(width, height), NULL);
  return TRUE;
}

float
WI_GetScaling(void)
{
  return (float)emscripten_get_device_pixel_ratio();
}

void
WI_MakeCurrentContext(void)
{
  emscripten_webgl_make_context_current(g_webgl_ctx);
}

void
WI_BeginPaint(void)
{
  emscripten_webgl_make_context_current(g_webgl_ctx);

  glClearColor(0.0, 1.0, 0.0, 1.0);
  glColorMask(1,1,1,1);
  glClear(GL_COLOR_BUFFER_BIT);
}

void
WI_EndPaint(void)
{
  // WebGL swapping is handled by the browser automatically
  // Yield to browser so it can present the frame via requestAnimationFrame
  emscripten_sleep(0);  // requires -sASYNCIFY=1
}

void
WI_BindFramebuffer(void)
{
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool_t
WI_CreateSurface(uint32_t width, uint32_t height)
{
  (void)width;
  (void)height;
  return FALSE;
}
