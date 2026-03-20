#include "../platform.h"
#include "webgl_local.h"

void
WI_Init(void)
{
  /* Size the canvas drawing buffer to CSS size × DPR before creating context
   * so iOS Safari allocates the correct framebuffer on the first frame. */
  double css_width, css_height;
  emscripten_get_element_css_size("#canvas", &css_width, &css_height);
  double dpr = emscripten_get_device_pixel_ratio();
  if (dpr < 1.0) dpr = 1.0;
  /* Truncate CSS size to integer before multiplying by DPR so that
   * physical_pixels / dpr is always an integer.  Without this, on devices
   * with an integer DPR (e.g. DPR=3 on iPhone), a fractional CSS size such
   * as 349.33px causes Emscripten to set the canvas CSS style to that same
   * fractional value, which in turn makes touch targetX/targetY non-integers
   * and triggers a SAFE_HEAP "attempt to write non-integer into integer heap"
   * error. */
  g_canvas_width  = (int)css_width;
  g_canvas_height = (int)css_height;
  emscripten_set_canvas_element_size("#canvas",
    (int)(g_canvas_width  * dpr + 0.5),
    (int)(g_canvas_height * dpr + 0.5));

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;
  attrs.alpha = EM_FALSE;
  attrs.depth = EM_TRUE;
  attrs.stencil = EM_TRUE;
  attrs.antialias = EM_FALSE;

  g_webgl_ctx = emscripten_webgl_create_context("#canvas", &attrs);
  if (g_webgl_ctx <= 0) {
    /* Fall back to WebGL 1 if WebGL 2 is not available */
    attrs.majorVersion = 1;
    g_webgl_ctx = emscripten_webgl_create_context("#canvas", &attrs);
  }

  emscripten_webgl_make_context_current(g_webgl_ctx);
  webgl_register_callbacks();
}

void
WI_Shutdown(void)
{
  emscripten_webgl_destroy_context(g_webgl_ctx);
  g_webgl_ctx = 0;
}

char const *
WI_GetPlatform(void)
{
  return "webgl";
}

char const *
WI_SettingsDirectory(void)
{
  return "/";
}

char const *
WI_ShareDirectory(void)
{
  return "/";
}

char const *
WI_LibDirectory(void)
{
  return "/";
}

longTime_t
WI_GetMilliseconds(void)
{
  return (longTime_t)emscripten_get_now();
}

void
WI_Sleep(longTime_t msec)
{
  (void)msec;
  /* Blocking sleep is not practical in a browser event loop.
   * Enable ASYNCIFY in emcc flags and call emscripten_sleep(msec) if needed. */
}

bool_t
WI_IsDarkTheme(void)
{
  /* Color-scheme detection would require a JS interop call; return FALSE for now. */
  return FALSE;
}

bool_t
WI_GetOpenFileName(WI_OpenFileName const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
WI_GetSaveFileName(WI_OpenFileName const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
WI_GetFolderName(WI_OpenFileName const *ofn)
{
  (void)ofn;
  return FALSE;
}

char const *
WI_KeynumToString(uint32_t keynum)
{
  static char tinystr[2];
  if (keynum == (uint32_t)-1)
    return "<KEY NOT FOUND>";
  keynum = keynum & 0xff;
  if (keynum > 32 && keynum < 127) {
    tinystr[0] = (char)keynum;
    tinystr[1] = 0;
    return tinystr;
  }
  return "<UNKNOWN KEYNUM>";
}
