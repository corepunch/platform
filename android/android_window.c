#include "android_local.h"
#include <android/configuration.h>
#include <android/native_window.h>
#include <pthread.h>

struct android_state android = {
  .display = EGL_NO_DISPLAY, .context = EGL_NO_CONTEXT, .surface = EGL_NO_SURFACE, .scale = 1,
};

void
android_update_scale(void)
{
  AConfiguration *config = android.app ? android.app->config : NULL;
  int32_t density = config ? AConfiguration_getDensity(config) : 0;
  if (density <= 0 || density == ACONFIGURATION_DENSITY_ANY || density == ACONFIGURATION_DENSITY_NONE)
    density = ACONFIGURATION_DENSITY_MEDIUM;
  /* Points follow Android's density-independent pixels: 160 dpi is 1x. */
  android.scale = (float)density / ACONFIGURATION_DENSITY_MEDIUM;
}

static bool_t
android_display_init(void)
{
  if (android.display != EGL_NO_DISPLAY) return TRUE;
  android.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (android.display == EGL_NO_DISPLAY || !eglInitialize(android.display, NULL, NULL)) {
    ANDROID_LOG("EGL initialization failed: 0x%x", eglGetError());
    android.display = EGL_NO_DISPLAY;
    return FALSE;
  }
  EGLint const attributes[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE
  };
  EGLint count = 0;
  if (!eglChooseConfig(android.display, attributes, &android.config, 1, &count) || count < 1) {
    ANDROID_LOG("No EGL configuration supports OpenGL ES 3");
    return FALSE;
  }
  EGLint const context[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
  android.context = eglCreateContext(android.display, android.config, EGL_NO_CONTEXT, context);
  if (android.context == EGL_NO_CONTEXT) {
    ANDROID_LOG("OpenGL ES 3 context creation failed: 0x%x", eglGetError());
    return FALSE;
  }
  return TRUE;
}

uint32_t
android_point_size(void)
{
  int w = (int)(android.pixel_width / android.scale + 0.5f);
  int h = (int)(android.pixel_height / android.scale + 0.5f);
  return MAKEDWORD(w, h);
}

/* The window comes and goes with the activity; the GL context outlives it, so
 * textures and buffers survive backgrounding. */
void
android_surface_create(void)
{
  ANativeWindow *window = android.app ? android.app->window : NULL;
  if (!window || !android_display_init() || android.surface != EGL_NO_SURFACE) return;
  EGLint format = 0;
  eglGetConfigAttrib(android.display, android.config, EGL_NATIVE_VISUAL_ID, &format);
  ANativeWindow_setBuffersGeometry(window, 0, 0, format);
  android.surface = eglCreateWindowSurface(android.display, android.config, window, NULL);
  if (android.surface == EGL_NO_SURFACE) {
    ANDROID_LOG("EGL window surface creation failed: 0x%x", eglGetError());
    return;
  }
  eglMakeCurrent(android.display, android.surface, android.surface, android.context);
  eglQuerySurface(android.display, android.surface, EGL_WIDTH, &android.pixel_width);
  eglQuerySurface(android.display, android.surface, EGL_HEIGHT, &android.pixel_height);
  android_update_scale();
  axPostMessageW(NULL, kEventWindowResized, android_point_size(), NULL);
  axPostMessageW(NULL, kEventWindowPaint, android_point_size(), NULL);
}

void
android_surface_destroy(void)
{
  if (android.surface == EGL_NO_SURFACE) return;
  eglMakeCurrent(android.display, EGL_NO_SURFACE, EGL_NO_SURFACE, android.context);
  eglDestroySurface(android.display, android.surface);
  android.surface = EGL_NO_SURFACE;
}

bool_t
axAndroidHasWindow(void)
{
  return android.surface != EGL_NO_SURFACE;
}

bool_t
axCreateWindow(char const *title, uint32_t width, uint32_t height, uint32_t flags)
{
  (void)title; (void)width; (void)height; (void)flags;
  /* The activity owns the window: wait for it once, while pumping lifecycle events. */
  while (!axAndroidHasWindow() && !android.destroy) axWaitMessage(100);
  if (!axAndroidHasWindow()) return FALSE;
  axPostMessageW(NULL, kEventWindowPaint, android_point_size(), NULL);
  return TRUE;
}

void
axMakeCurrentContext(void)
{
  if (android.context == EGL_NO_CONTEXT) return;
  eglMakeCurrent(android.display, android.surface, android.surface, android.context);
}

void axBeginPaint(void) { axMakeCurrentContext(); }

void
axEndPaint(void)
{
  if (android.surface == EGL_NO_SURFACE) return;
  if (!eglSwapBuffers(android.display, android.surface) && eglGetError() == EGL_BAD_SURFACE) {
    android_surface_destroy();
    android_surface_create();
  }
}

void axBindFramebuffer(void) { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

bool_t
axCreateSurface(uint32_t width, uint32_t height)
{
  (void)width; (void)height;
  return FALSE;
}

bool_t
axSetSwapInterval(int interval)
{
  return android.display != EGL_NO_DISPLAY && eglSwapInterval(android.display, interval) == EGL_TRUE;
}

float axGetScaling(void) { return android.scale; }

uint32_t
axGetSize(struct AXsize *size)
{
  uint32_t points = android_point_size();
  if (size) { size->width = LOWORD(points); size->height = HIWORD(points); }
  return points;
}

bool_t
axSetSize(uint32_t width, uint32_t height, bool_t centered)
{
  (void)width; (void)height; (void)centered;
  return FALSE; /* The activity decides the window size. */
}

/* Insets arrive on the UI thread; the engine reads them on its own thread. */
static pthread_mutex_t insets_lock = PTHREAD_MUTEX_INITIALIZER;

void
axAndroidSetInsets(float left, float top, float right, float bottom, float keyboard)
{
  float values[] = { left, top, right, bottom };
  pthread_mutex_lock(&insets_lock);
  bool_t changed = keyboard != android.keyboard || memcmp(values, android.safe, sizeof(values));
  memcpy(android.safe, values, sizeof(values));
  android.keyboard = keyboard;
  pthread_mutex_unlock(&insets_lock);
  if (changed) axAndroidPostMessage((struct AXmessage){ .message = kEventWindowResized });
}

void
axAndroidGetSafeArea(float *left, float *top, float *right, float *bottom)
{
  pthread_mutex_lock(&insets_lock);
  if (left) *left = android.safe[0];
  if (top) *top = android.safe[1];
  if (right) *right = android.safe[2];
  if (bottom) *bottom = android.safe[3];
  pthread_mutex_unlock(&insets_lock);
}

float
axAndroidGetKeyboardInset(void)
{
  pthread_mutex_lock(&insets_lock);
  float keyboard = android.keyboard;
  pthread_mutex_unlock(&insets_lock);
  return keyboard;
}

void
axInit(void)
{
  android_update_scale();
}

void
axShutdown(void)
{
  axClearMessageQueue();
  android_surface_destroy();
  if (android.context != EGL_NO_CONTEXT) eglDestroyContext(android.display, android.context);
  if (android.display != EGL_NO_DISPLAY) eglTerminate(android.display);
  android.context = EGL_NO_CONTEXT;
  android.display = EGL_NO_DISPLAY;
}

void R_ReleaseIOSurface(unsigned iosurface) { (void)iosurface; }
struct _IMAGE *R_CreatetextureFromIOSurface(unsigned surfaceID) { (void)surfaceID; return NULL; }
unsigned R_CreateIOSurface(unsigned w, unsigned h, unsigned texnum) { (void)w; (void)h; (void)texnum; return (unsigned)-1; }
