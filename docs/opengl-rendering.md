# OpenGL Rendering

The platform library manages an OpenGL / EGL context for you.  You call
`axBeginPaint` before drawing and `axEndPaint` to present the frame.

---

## Basic paint loop

```c
axBeginPaint();

glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

draw_scene();

axEndPaint();
```

`axBeginPaint` makes the context current on the calling thread and flushes
any pending platform events (Wayland display flush, macOS Cocoa run-loop tick).
`axEndPaint` swaps the back and front buffers.

If you need to issue GL commands outside the paint loop — for example when
loading textures on startup — call `axMakeCurrentContext` directly:

```c
axMakeCurrentContext();
upload_textures();
```

---

## VSync / swap interval

```c
axSetSwapInterval(1);   /* lock to display refresh rate */
axSetSwapInterval(0);   /* present as fast as possible  */
```

Returns `FALSE` on platforms where swap-interval control is unavailable.  The
default after `axCreateWindow` is platform-dependent (usually vsync-on).

Typical pattern for a game that wants to run uncapped:

```c
axCreateWindow("Game", 1280, 720, AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_RESIZABLE);
axSetSwapInterval(0);
```

---

## HiDPI / Retina rendering

On Retina Macs and high-DPI displays the logical window size differs from the
physical framebuffer size.  Always use `axGetScaling()` to compute the correct
viewport dimensions:

```c
struct AXsize logical;
axGetSize(&logical);

float scale = axGetScaling();   /* 2.0 on Retina, 1.0 elsewhere */

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
    float scale = axGetScaling();
    glViewport(0, 0, (GLsizei)(w * scale), (GLsizei)(h * scale));
    update_projection(w, h);
    break;
}
```

---

## Hidden and off-screen rendering

`axCreateSurface` allocates an IOSurface-backed off-screen framebuffer on
macOS. Orion's `UI_INIT_HIDDEN` uses it when available and otherwise creates an
`AX_WINDOW_HIDDEN` platform window, providing a portable OpenGL context without
showing application UI. Applications should render to their own framebuffer
object when they need a specific output size or pixel format.

```c
axCreateSurface(1920, 1080);

/* render off-screen */
axMakeCurrentContext();
axBindFramebuffer();

glClear(GL_COLOR_BUFFER_BIT);
draw_scene();

/* present */
axEndPaint();
```

`axCreateSurface` may return `FALSE` on platforms without a native off-screen
surface; this indicates that the caller should use a hidden window context.

---

## Multiple contexts / threads

The library manages a single context.  If you render from a background thread:

1. Ensure the main thread never calls `axBeginPaint` / `axMakeCurrentContext`
   concurrently.
2. Call `axMakeCurrentContext()` at the start of your render thread.
3. Use `axEndPaint()` from the same thread to present.
