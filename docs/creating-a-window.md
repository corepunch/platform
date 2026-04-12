# Creating a Window

Every platform application follows the same three steps: initialise, create a
window, shut down.

```c
axInit();

if (!axCreateWindow("My App", 800, 600, 0)) {
    fprintf(stderr, "window creation failed\n");
    return 1;
}

/* … main loop … */

axShutdown();
```

`axInit` must be called once before anything else.  `axShutdown` releases
every resource the library owns, including the OpenGL context.

---

## Window flags

`axCreateWindow` takes a bitmask as its fourth argument.  Pass `0` for
sensible defaults: a decorated, resizable, double-buffered window.

| Flag | Bit | What it does |
|------|----:|---|
| `AX_WINDOW_DOUBLEBUFFER` | `1<<0` | Double-buffered GL context (already the default; useful for clarity) |
| `AX_WINDOW_FULLSCREEN`   | `1<<1` | Occupies the full screen, no decorations |
| `AX_WINDOW_BORDERLESS`   | `1<<2` | No title bar or OS chrome |
| `AX_WINDOW_RESIZABLE`    | `1<<3` | User can drag the edges to resize |
| `AX_WINDOW_HIDDEN`       | `1<<4` | Window starts invisible |
| `AX_WINDOW_HIGHDPI`      | `1<<5` | Request a Retina / HiDPI framebuffer |

### Resizability gotcha

Passing **any** non-zero flags disables the default resizable behaviour.
Always add `AX_WINDOW_RESIZABLE` explicitly when you also use other flags:

```c
/* Fixed-size borderless window — intentional */
axCreateWindow("HUD", 400, 300, AX_WINDOW_BORDERLESS);

/* Borderless AND resizable */
axCreateWindow("Panel", 400, 300,
    AX_WINDOW_BORDERLESS | AX_WINDOW_RESIZABLE);
```

### Fullscreen

```c
axCreateWindow("Game", 1920, 1080,
    AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_FULLSCREEN);
```

On Windows the requested size is ignored and the display resolution is used
instead.  On other platforms pass `0, 0` to let the library pick the
resolution.

### Starting hidden

Useful when you want to finish loading assets before the window appears:

```c
axCreateWindow("App", 1280, 720,
    AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_RESIZABLE | AX_WINDOW_HIDDEN);

load_assets();

/* make it visible — platform-specific show call, or just post a paint */
axPostMessageW(NULL, kEventWindowPaint, 0, NULL);
```

### HiDPI / Retina

```c
axCreateWindow("Crisp", 1280, 720,
    AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_RESIZABLE | AX_WINDOW_HIGHDPI);
```

After creation, use `axGetScaling()` to find out the actual pixel multiplier
and size your framebuffer accordingly:

```c
float scale = axGetScaling();   /* e.g. 2.0 on a Retina Mac */
uint32_t fbw = (uint32_t)(logical_width  * scale);
uint32_t fbh = (uint32_t)(logical_height * scale);
glViewport(0, 0, (GLsizei)fbw, (GLsizei)fbh);
```

---

## Resizing after creation

```c
/* Resize to 1024×768, re-centred on screen */
axSetSize(1024, 768, TRUE);

/* Query the current size */
struct AXsize sz;
axGetSize(&sz);
printf("%u x %u\n", sz.width, sz.height);
```

`axSetSize` returns `FALSE` on platforms that do not support programmatic
resize (e.g. fullscreen WebGL).

---

## Platform notes

| Feature | Win | Wayland | X11 | macOS | WebGL |
|---------|:---:|:-------:|:---:|:-----:|:-----:|
| Borderless      | ✓ | ✓ | ✓ | ✓ | — |
| Fullscreen      | ✓ | ✓ | ✓ | ✓ | ✓ |
| Resizable       | ✓ | ✓ | ✓ | ✓ | — |
| Hidden at start | ✓ | ✓ | ✓ | ✓ | — |
| HiDPI           | — | — | — | ✓ | ✓ |

"—" means the flag is silently ignored.
