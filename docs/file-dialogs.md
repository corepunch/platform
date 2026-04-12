# File Dialogs

The platform library wraps the native file-picker dialogs on each platform.
All three functions are synchronous and modal — they block until the user
confirms or cancels.

---

## The `AXopenfilename` struct

```c
typedef struct {
    char       *lpstrFile;   /* output buffer (UTF-8, null-terminated) */
    uint32_t    nMaxFile;    /* size of lpstrFile in bytes              */
    char const *lpstrFilter; /* optional file-type filter               */
    char const *lpstrTitle;  /* optional dialog title (NULL = default)  */
    uint32_t    Flags;       /* OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST   */
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

`lpstrFilter` uses Windows-style double-null-terminated filter pairs.  On
non-Windows platforms the filter is currently ignored, but the dialog still
works.

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
