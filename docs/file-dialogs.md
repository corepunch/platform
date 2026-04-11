# File Dialogs

The platform library wraps the native file-picker dialogs on each platform.
All three functions are synchronous and modal — they block until the user
confirms or cancels.

---

## The `WI_OpenFileName` struct

```c
typedef struct {
    char       *lpstrFile;   /* output buffer (UTF-8, null-terminated) */
    uint32_t    nMaxFile;    /* size of lpstrFile in bytes              */
    char const *lpstrFilter; /* optional file-type filter               */
    char const *lpstrTitle;  /* optional dialog title (NULL = default)  */
    uint32_t    Flags;       /* OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST   */
} WI_OpenFileName;
```

---

## Open file

```c
char path[1024] = {0};

WI_OpenFileName ofn = {
    .lpstrFile   = path,
    .nMaxFile    = sizeof(path),
    .lpstrFilter = "Images\0*.png;*.jpg\0All files\0*.*\0",
    .lpstrTitle  = "Open image",
    .Flags       = OFN_FILEMUSTEXIST,
};

if (WI_GetOpenFileName(&ofn)) {
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

WI_OpenFileName ofn = {
    .lpstrFile   = path,
    .nMaxFile    = sizeof(path),
    .lpstrFilter = "PNG image\0*.png\0",
    .lpstrTitle  = "Save as",
};

if (WI_GetSaveFileName(&ofn)) {
    export_png(path);
}
```

---

## Folder picker

```c
char dir[1024] = {0};

WI_OpenFileName ofn = {
    .lpstrFile = dir,
    .nMaxFile  = sizeof(dir),
    .lpstrTitle = "Choose export directory",
};

if (WI_GetFolderName(&ofn)) {
    set_export_dir(dir);
}
```

---

## Platform support

| Function | Windows | macOS | Linux (X11) | Wayland | WebGL |
|----------|:-------:|:-----:|:-----------:|:-------:|:-----:|
| `WI_GetOpenFileName` | ✓ | ✓ | ✓ | — | — |
| `WI_GetSaveFileName` | ✓ | ✓ | ✓ | — | — |
| `WI_GetFolderName`   | ✓ | ✓ | ✓ | — | — |

On unsupported platforms the functions return `FALSE` immediately and leave
`lpstrFile` unchanged.  Check the return value before using the buffer.
