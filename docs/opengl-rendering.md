# OpenGL Rendering

The platform library manages an OpenGL / EGL context for you.  You call
`WI_BeginPaint` before drawing and `WI_EndPaint` to present the frame.

---

## Basic paint loop

```c
WI_BeginPaint();

glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

draw_scene();

WI_EndPaint();
```

`WI_BeginPaint` makes the context current on the calling thread and flushes
any pending platform events (Wayland display flush, macOS Cocoa run-loop tick).
`WI_EndPaint` swaps the back and front buffers.

If you need to issue GL commands outside the paint loop — for example when
loading textures on startup — call `WI_MakeCurrentContext` directly:

```c
WI_MakeCurrentContext();
upload_textures();
```

---

## VSync / swap interval

```c
WI_SetSwapInterval(1);   /* lock to display refresh rate */
WI_SetSwapInterval(0);   /* present as fast as possible  */
```

Returns `FALSE` on platforms where swap-interval control is unavailable.  The
default after `WI_CreateWindow` is platform-dependent (usually vsync-on).

Typical pattern for a game that wants to run uncapped:

```c
WI_CreateWindow("Game", 1280, 720, WI_WINDOW_DOUBLEBUFFER | WI_WINDOW_RESIZABLE);
WI_SetSwapInterval(0);
```

---

## HiDPI / Retina rendering

On Retina Macs and high-DPI displays the logical window size differs from the
physical framebuffer size.  Always use `WI_GetScaling()` to compute the correct
viewport dimensions:

```c
struct WI_Size logical;
WI_GetSize(&logical);

float scale = WI_GetScaling();   /* 2.0 on Retina, 1.0 elsewhere */

GLsizei fb_w = (GLsizei)(logical.width  * scale);
GLsizei fb_h = (GLsizei)(logical.height * scale);

glViewport(0, 0, fb_w, fb_h);
```

Re-run this after every `kEventWindowResized` and
`kEventWindowChangedScreen` event.

---

## Handling resize

```c
case kEventWindowResized: {
    uint32_t w = LOWORD(msg.wParam);
    uint32_t h = HIWORD(msg.wParam);
    float scale = WI_GetScaling();
    glViewport(0, 0, (GLsizei)(w * scale), (GLsizei)(h * scale));
    update_projection(w, h);
    break;
}
```

---

## Off-screen surfaces (macOS only)

`WI_CreateSurface` allocates an IOSurface-backed off-screen framebuffer.  Bind
it with `WI_BindFramebuffer` before rendering:

```c
WI_CreateSurface(1920, 1080);

/* render off-screen */
WI_MakeCurrentContext();
WI_BindFramebuffer();

glClear(GL_COLOR_BUFFER_BIT);
draw_scene();

/* present */
WI_EndPaint();
```

`WI_CreateSurface` returns `FALSE` on all platforms except macOS.

---

## Multiple contexts / threads

The library manages a single context.  If you render from a background thread:

1. Ensure the main thread never calls `WI_BeginPaint` / `WI_MakeCurrentContext`
   concurrently.
2. Call `WI_MakeCurrentContext()` at the start of your render thread.
3. Use `WI_EndPaint()` from the same thread to present.
