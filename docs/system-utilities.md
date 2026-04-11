# System Utilities

---

## Timing

### Millisecond clock

`WI_GetMilliseconds` returns a monotonic timestamp.  The epoch is arbitrary —
use the *difference* between two calls to measure elapsed time:

```c
longTime_t start = WI_GetMilliseconds();
do_work();
longTime_t elapsed = WI_GetMilliseconds() - start;
printf("took %lu ms\n", elapsed);
```

### Sleep

```c
WI_Sleep(100);   /* pause for 100 ms */
WI_Sleep(0);     /* yield the CPU without sleeping */
```

### Frame-rate limiter

```c
while (running) {
    longTime_t frame_start = WI_GetMilliseconds();

    WI_BeginPaint();
    render();
    WI_EndPaint();

    longTime_t frame_ms = WI_GetMilliseconds() - frame_start;
    if (frame_ms < 16)
        WI_Sleep(16 - frame_ms);   /* cap at ~60 FPS */
}
```

---

## Dark-theme detection

Check once at startup and again whenever the system theme changes:

```c
void apply_theme(void) {
    if (WI_IsDarkTheme()) {
        set_colours(&dark_palette);
    } else {
        set_colours(&light_palette);
    }
}

/* call at startup */
apply_theme();
```

| Platform | Detection method |
|----------|-----------------|
| Windows  | `HKCU\...\Themes\Personalize\AppsUseLightTheme` registry key |
| macOS    | `NSApp effectiveAppearance` |
| Wayland  | `GTK_THEME` / `QT_STYLE_OVERRIDE` environment variables |
| X11, QNX, WebGL | Always returns `FALSE` |

---

## Platform identification

```c
printf("running on: %s\n", WI_GetPlatform());
```

Returns one of: `"windows"`, `"macos"`, `"linux (wayland)"`,
`"linux (x11)"`, `"qnx"`, `"webgl"`.

---

## Application directories

The library resolves three conventional paths based on the executable location
and OS conventions.  All paths are null-terminated UTF-8 and are valid for the
lifetime of the process — do not free them.

### Settings directory

Per-user mutable data: preferences, save files, caches.

```c
char path[1024];
snprintf(path, sizeof(path), "%s/config.ini", WI_SettingsDirectory());
load_config(path);
```

| Platform | Typical path |
|----------|-------------|
| Windows  | `%APPDATA%\<appname>\` |
| macOS    | `~/Library/Application Support/<appname>/` |
| Linux    | `~/.config/<appname>/` or `$XDG_CONFIG_HOME/<appname>/` |

The directory is created automatically if it does not exist.

### Share directory

Read-only bundled data: shaders, fonts, default assets.

```c
char shader_path[1024];
snprintf(shader_path, sizeof(shader_path),
         "%s/shaders/basic.vert", WI_ShareDirectory());
load_shader(shader_path);
```

| Platform | Typical path |
|----------|-------------|
| Windows  | Same directory as the executable |
| macOS    | `<bundle>/Contents/Resources/` |
| Linux    | `<exe>/../share/<appname>/` |

### Lib directory

Platform-specific dynamic libraries shipped with the application.

```c
char plugin_path[1024];
snprintf(plugin_path, sizeof(plugin_path),
         "%s/plugins/renderer.so", WI_LibDirectory());
dlopen(plugin_path, RTLD_LAZY);
```

| Platform | Typical path |
|----------|-------------|
| Windows  | Same directory as the executable |
| macOS    | Same as `WI_ShareDirectory()` |
| Linux    | `<exe>/../lib/<appname>/` |
