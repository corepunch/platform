#include "../platform.h"
#include "webgl_local.h"

void
axInit(void)
{
  /* Size the canvas drawing buffer to CSS size x DPR before creating context
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
axShutdown(void)
{
  emscripten_webgl_destroy_context(g_webgl_ctx);
  g_webgl_ctx = 0;
}

char const *
axGetPlatform(void)
{
  return "webgl";
}

char const *
axSettingsDirectory(void)
{
  return "/";
}

char const *
axShareDirectory(void)
{
  return "/";
}

char const *
axLibDirectory(void)
{
  return "/";
}

longTime_t
axGetMilliseconds(void)
{
  return (longTime_t)emscripten_get_now();
}

void
axSleep(longTime_t msec)
{
  (void)msec;
  /* Blocking sleep is not practical in a browser event loop.
   * Enable ASYNCIFY in emcc flags and call emscripten_sleep(msec) if needed. */
}

bool_t
axIsDarkTheme(void)
{
  /* Color-scheme detection would require a JS interop call; return FALSE for now. */
  return FALSE;
}

bool_t
axGetOpenFileName(AXopenfilename const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
axGetSaveFileName(AXopenfilename const *ofn)
{
  (void)ofn;
  return FALSE;
}

bool_t
axGetFolderName(AXopenfilename const *ofn)
{
  (void)ofn;
  return FALSE;
}

char const *
axKeynumToString(uint32_t keynum)
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

void *
axDynlibOpen(char const *path)
{
  (void)path;
  return NULL;
}

void *
axDynlibSym(void *handle, char const *sym)
{
  (void)handle;
  (void)sym;
  return NULL;
}

void
axDynlibClose(void *handle)
{
  (void)handle;
}

char const *
axDynlibError(void)
{
  return "dynamic library loading is not supported on WebGL";
}

bool_t
axMkDir(char const *path)
{
  (void)path;
  return FALSE;
}

bool_t
axListDir(char const *path, AXDirCallback cb, void *userdata)
{
  (void)path;
  (void)cb;
  (void)userdata;
  return FALSE;
}

bool_t
axPathExists(char const *path)
{
  (void)path;
  return FALSE;
}

bool_t
axGetCwd(char *buf, size_t sz)
{
  if (buf && sz > 0)
    buf[0] = '\0';
  return FALSE;
}

bool_t
axSettingsSave(char const *name, void const *data, size_t size)
{
  (void)name;
  (void)data;
  (void)size;
  return FALSE;
}

bool_t
axSettingsLoad(char const *name, void *data, size_t capacity, size_t *out_size)
{
  (void)name;
  (void)data;
  (void)capacity;
  if (out_size)
    *out_size = 0;
  return FALSE;
}
