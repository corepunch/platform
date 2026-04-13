# System Utilities

---

## Timing

### Millisecond clock

`axGetMilliseconds` returns a monotonic timestamp.  The epoch is arbitrary —
use the *difference* between two calls to measure elapsed time:

```c
longTime_t start = axGetMilliseconds();
do_work();
longTime_t elapsed = axGetMilliseconds() - start;
printf("took %lu ms\n", elapsed);
```

### Sleep

```c
axSleep(100);   /* pause for 100 ms */
axSleep(0);     /* yield the CPU without sleeping */
```

### Frame-rate limiter

```c
while (running) {
    longTime_t frame_start = axGetMilliseconds();

    axBeginPaint();
    render();
    axEndPaint();

    longTime_t frame_ms = axGetMilliseconds() - frame_start;
    if (frame_ms < 16)
        axSleep(16 - frame_ms);   /* cap at ~60 FPS */
}
```

---

## Dark-theme detection

Check once at startup and again whenever the system theme changes:

```c
void apply_theme(void) {
    if (axIsDarkTheme()) {
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
printf("running on: %s\n", axGetPlatform());
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
snprintf(path, sizeof(path), "%s/config.ini", axSettingsDirectory());
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
         "%s/shaders/basic.vert", axShareDirectory());
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
         "%s/plugins/renderer.so", axLibDirectory());
dlopen(plugin_path, RTLD_LAZY);
```

| Platform | Typical path |
|----------|-------------|
| Windows  | Same directory as the executable |
| macOS    | Same as `axShareDirectory()` |
| Linux    | `<exe>/../lib/<appname>/` |

---

## Dynamic library loading

Use `axDynlibOpen` / `axDynlibSym` / `axDynlibClose` to load plugins or
optional shared libraries at run time.  The `AX_DYNLIB_EXT` macro provides
the correct file-name extension for the current platform so library names can
be written portably.

### Basic usage

```c
/* Build a portable path to the plugin */
char path[1024];
snprintf(path, sizeof(path), "%s/myplugin" AX_DYNLIB_EXT, axLibDirectory());

void *lib = axDynlibOpen(path);
if (!lib) {
    fprintf(stderr, "load error: %s\n", axDynlibError());
    return;
}

/* Look up a function by name */
typedef int (*plugin_init_fn)(void);
plugin_init_fn init = (plugin_init_fn)axDynlibSym(lib, "plugin_init");
if (init)
    init();

axDynlibClose(lib);
```

### Platform support

| Platform | Backend |
|----------|---------|
| macOS    | `dlopen` / `dlsym` / `dlclose` / `dlerror` |
| Linux (Wayland / X11) | `dlopen` / `dlsym` / `dlclose` / `dlerror` |
| Windows  | `LoadLibraryA` / `GetProcAddress` / `FreeLibrary` / `FormatMessage` |
| QNX      | `dlopen` / `dlsym` / `dlclose` / `dlerror` |
| WebGL    | Stubs — always return `NULL` (dynamic loading is not supported in browsers) |
