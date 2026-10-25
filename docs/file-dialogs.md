# File Dialogs

The platform library wraps the native file-picker dialogs on each platform.
All three functions are synchronous and modal — they block until the user
confirms or cancels.

---

## The `AXopenfilename` struct

```c
typedef struct {
    char       *lpstrFile;   /* in/out: default path and selected UTF-8 path */
    uint32_t    nMaxFile;    /* size of lpstrFile in bytes              */
    char const *lpstrFilter; /* optional file-type filter               */
    char const *lpstrTitle;  /* optional dialog title (NULL = default)  */
    uint32_t    Flags;       /* OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST   */
    uint32_t    nFilterIndex; /* initial filter, 1-based; 0 means first */
} AXopenfilename;
```

---

## Open file

```c
char path[1024] = {0};

AXopenfilename ofn = {
    .lpstrFile   = path,
    .nMaxFile    = sizeof(path),
    .lpstrFilter = "Images\0*.png;*.jpg\0All files\0*.*\0",
    .lpstrTitle  = "Open image",
    .Flags       = OFN_FILEMUSTEXIST,
};

if (axGetOpenFileName(&ofn)) {
    load_image(path);
}
```

`lpstrFilter` uses Windows-style double-null-terminated filter pairs. macOS
supports semicolon-separated extensions, an All Files (`*.*` or `*`) choice,
and a filter popup when multiple pairs are supplied. Other non-Windows backends
currently ignore the filter. `lpstrFile` must initially be null-terminated.

On macOS, all panels honor `lpstrTitle` and the default path in `lpstrFile`.
Cancel leaves the buffer unchanged. An invalid buffer or a selected path that
does not fit is rejected with a diagnostic instead of returning a truncated path.

`axFileDialogsRequireNative()` returns true for a macOS process signed with
`com.apple.security.app-sandbox = true`, and for iOS. It returns false on other
platforms. Orion uses this capability to route sandboxed macOS Open, Save, and
folder dialogs through native panels; ordinary desktop builds retain Orion's
custom dialogs. The macOS query uses the process's signed entitlement rather
than an environment variable or a build flag.

Native macOS panels grant access through Powerbox before returning. The
path-based API keeps that implicit access available for subsequent reads and
saves during the process lifetime; it does not create persistent bookmarks.
Reopening a stored path after relaunch requires selecting it again or a
document layer that manages security-scoped bookmarks. Apps need the
`com.apple.security.files.user-selected.read-write` sandbox entitlement to save
outside their container. See Apple's
[sandbox file access guide](https://developer.apple.com/documentation/security/accessing-files-from-the-macos-app-sandbox).

---

## Save file

```c
char path[1024] = {0};
strncpy(path, "untitled.png", sizeof(path));

AXopenfilename ofn = {
    .lpstrFile   = path,
    .nMaxFile    = sizeof(path),
    .lpstrFilter = "PNG image\0*.png\0",
    .lpstrTitle  = "Save as",
};

if (axGetSaveFileName(&ofn)) {
    export_png(path);
}
```

---

## Folder picker

```c
char dir[1024] = {0};

AXopenfilename ofn = {
    .lpstrFile = dir,
    .nMaxFile  = sizeof(dir),
    .lpstrTitle = "Choose export directory",
};

if (axGetFolderName(&ofn)) {
    set_export_dir(dir);
}
```

---

## Platform support

| Function | Windows | macOS | Linux (X11) | Wayland | WebGL |
|----------|:-------:|:-----:|:-----------:|:-------:|:-----:|
| `axGetOpenFileName` | ✓ | ✓ | ✓ | — | — |
| `axGetSaveFileName` | ✓ | ✓ | ✓ | — | — |
| `axGetFolderName`   | ✓ | ✓ | ✓ | — | — |

On unsupported platforms the functions return `FALSE` immediately and leave
`lpstrFile` unchanged.  Check the return value before using the buffer.
