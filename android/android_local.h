#ifndef __ANDROID_LOCAL_H__
#define __ANDROID_LOCAL_H__

#include "../platform.h"
#include "ax_android.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#define ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, "orca", __VA_ARGS__)

struct android_state {
  struct android_app *app;
  EGLDisplay display;
  EGLConfig config;
  EGLContext context;
  EGLSurface surface;
  int pixel_width, pixel_height; /* surface size in pixels */
  float scale;                   /* pixels per point */
  float safe[4], keyboard;       /* left, top, right, bottom; keyboard, in points */
  bool_t focused, destroy;
};

extern struct android_state android;

/* android_window.c */
void android_surface_create(void);
void android_surface_destroy(void);
void android_update_scale(void);
uint32_t android_point_size(void);

/* android_event.c */
void android_handle_cmd(struct android_app *app, int32_t cmd);
/* Runs looper callbacks and drains timers and the inbox for up to timeout_ms. */
int android_pump(int timeout_ms);
int32_t android_handle_input(struct android_app *app, AInputEvent *event);

#endif
