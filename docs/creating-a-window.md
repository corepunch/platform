# Creating a Window

Every platform application follows the same three steps: initialise, create a
window, shut down.

```c
WI_Init();

if (!WI_CreateWindow("My App", 800, 600, 0)) {
    fprintf(stderr, "window creation failed\n");
    return 1;
}

/* … main loop … */

WI_Shutdown();
```

`WI_Init` must be called once before anything else.  `WI_Shutdown` releases
every resource the library owns, including the OpenGL context.

---

## Window flags

`WI_CreateWindow` takes a bitmask as its fourth argument.  Pass `0` for
sensible defaults: a decorated, resizable, double-buffered window.

| Flag | Bit | What it does |
|------|----:|---|
| `WI_WINDOW_DOUBLEBUFFER` | `1<<0` | Double-buffered GL context (already the default; useful for clarity) |
| `WI_WINDOW_FULLSCREEN`   | `1<<1` | Occupies the full screen, no decorations |
| `WI_WINDOW_BORDERLESS`   | `1<<2` | No title bar or OS chrome |
| `WI_WINDOW_RESIZABLE`    | `1<<3` | User can drag the edges to resize |
| `WI_WINDOW_HIDDEN`       | `1<<4` | Window starts invisible |
| `WI_WINDOW_HIGHDPI`      | `1<<5` | Request a Retina / HiDPI framebuffer |

### Resizability gotcha

Passing **any** non-zero flags disables the default resizable behaviour.
Always add `WI_WINDOW_RESIZABLE` explicitly when you also use other flags:

```c
/* Fixed-size borderless window — intentional */
WI_CreateWindow("HUD", 400, 300, WI_WINDOW_BORDERLESS);

/* Borderless AND resizable */
WI_CreateWindow("Panel", 400, 300,
    WI_WINDOW_BORDERLESS | WI_WINDOW_RESIZABLE);
```

### Fullscreen

```c
WI_CreateWindow("Game", 1920, 1080,
    WI_WINDOW_DOUBLEBUFFER | WI_WINDOW_FULLSCREEN);
```

On Windows the requested size is ignored and the display resolution is used
instead.  On other platforms pass `0, 0` to let the library pick the
resolution.

### Starting hidden

Useful when you want to finish loading assets before the window appears:

```c
WI_CreateWindow("App", 1280, 720,
    WI_WINDOW_DOUBLEBUFFER | WI_WINDOW_RESIZABLE | WI_WINDOW_HIDDEN);

load_assets();

/* make it visible — platform-specific show call, or just post a paint */
WI_PostMessageW(NULL, kEventWindowPaint, 0, NULL);
```

### HiDPI / Retina

```c
WI_CreateWindow("Crisp", 1280, 720,
    WI_WINDOW_DOUBLEBUFFER | WI_WINDOW_RESIZABLE | WI_WINDOW_HIGHDPI);
```

After creation, use `WI_GetScaling()` to find out the actual pixel multiplier
and size your framebuffer accordingly:

```c
float scale = WI_GetScaling();   /* e.g. 2.0 on a Retina Mac */
uint32_t fbw = (uint32_t)(logical_width  * scale);
uint32_t fbh = (uint32_t)(logical_height * scale);
glViewport(0, 0, (GLsizei)fbw, (GLsizei)fbh);
```

---

## Resizing after creation

```c
/* Resize to 1024×768, re-centred on screen */
WI_SetSize(1024, 768, TRUE);

/* Query the current size */
struct WI_Size sz;
WI_GetSize(&sz);
printf("%u x %u\n", sz.width, sz.height);
```

`WI_SetSize` returns `FALSE` on platforms that do not support programmatic
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
