# Directory Operations — Porting Guide

The platform library provides three portable directory functions that replace
raw POSIX calls (`mkdir` / `opendir` / `readdir` / `getcwd`) and their Win32
equivalents.  Using these wrappers keeps your code free of `#ifdef` guards and
compiles correctly on Windows (MinGW/MSVC), macOS, Linux, QNX, and WebGL.

---

## Why bother?

| Operation | POSIX                    | Windows (Win32 / MinGW)              |
|-----------|--------------------------|--------------------------------------|
| Create dir | `mkdir(path, mode)`     | `CreateDirectoryA(path, NULL)` — **no mode argument** |
| List dir  | `opendir` / `readdir` / `closedir` | `FindFirstFileA` / `FindNextFileA` / `FindClose` |
| Get cwd   | `getcwd(buf, sz)`        | `GetCurrentDirectoryA(sz, buf)` — reversed arg order |

A direct POSIX call like `mkdir(path, 0777)` will fail to compile on
Windows/MinGW because `mkdir` there takes only **one** argument:

```c
// commctl/filepicker.c — THIS BREAKS ON WINDOWS:
if (mkdir(full, 0777) != 0) { /* error */ }
//         ^^^^^^ extra argument — MinGW mkdir expects only (path)
```

Replacing it with `axMkDir` fixes the build on all platforms.

---

## API reference

### `axMkDir` — create a directory

```c
bool_t axMkDir(char const *path);
```

Creates the directory at `path`.  Returns `TRUE` on success or if the
directory already exists.  Returns `FALSE` on permission errors, missing
parent, etc.

> **Note:** only one level of directory is created.  For a recursive
> `mkdir -p`-style helper see the example below.

### `axListDir` — enumerate directory entries

```c
typedef struct {
    char   name[256];    /* entry name only (not full path) */
    bool_t is_directory;
    bool_t is_hidden;    /* dot-files on POSIX; FILE_ATTRIBUTE_HIDDEN on Windows */
    size_t size;         /* 0 for directories */
    time_t modified;     /* UTC Unix timestamp */
} AXdirent;

typedef bool_t (*AXDirCallback)(AXdirent const *entry, void *userdata);

bool_t axListDir(char const *path, AXDirCallback cb, void *userdata);
```

Iterates every entry in `path` (excluding `.` and `..`), calling `cb` for
each one.  Return `FALSE` from the callback to stop early.  The function
itself returns `TRUE` when the directory was opened successfully, `FALSE`
otherwise.

### `axGetCwd` — current working directory

```c
bool_t axGetCwd(char *buf, size_t sz);
```

Writes the current working directory into `buf` (null-terminated UTF-8).
Returns `TRUE` on success, `FALSE` if the buffer is too small or the path
cannot be determined.

---

## Porting examples

### 1 — Replace `mkdir`

**Before (POSIX-only — breaks on Windows):**
```c
#include <sys/stat.h>

if (mkdir(full_path, 0777) != 0 && errno != EEXIST) {
    fprintf(stderr, "mkdir failed\n");
}
```

**After (cross-platform):**
```c
#include "platform.h"

if (!axMkDir(full_path)) {
    fprintf(stderr, "axMkDir failed\n");
}
```

---

### 2 — Replace `opendir` / `readdir` / `closedir`

**Before (POSIX-only):**
```c
#include <dirent.h>
#include <sys/stat.h>

DIR *dir = opendir(path);
if (dir) {
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        printf("%s  (%s, %zu bytes)\n",
               ent->d_name,
               S_ISDIR(st.st_mode) ? "dir" : "file",
               (size_t)st.st_size);
    }
    closedir(dir);
}
```

**After (cross-platform):**
```c
#include "platform.h"

static bool_t print_entry(AXdirent const *e, void *userdata) {
    (void)userdata;
    printf("%s  (%s, %zu bytes)\n",
           e->name,
           e->is_directory ? "dir" : "file",
           e->size);
    return TRUE; /* continue */
}

axListDir(path, print_entry, NULL);
```

---

### 3 — Replace `getcwd`

**Before (POSIX-only — `getcwd` is in `<direct.h>` on MSVC and arg order differs):**
```c
#include <unistd.h>  /* POSIX */
/* #include <direct.h>  // Windows */

char cwd[1024];
if (getcwd(cwd, sizeof(cwd))) {
    printf("cwd: %s\n", cwd);
}
```

**After (cross-platform):**
```c
#include "platform.h"

char cwd[1024];
if (axGetCwd(cwd, sizeof(cwd))) {
    printf("cwd: %s\n", cwd);
}
```

---

### 4 — Collect matching files from a directory

```c
#include "platform.h"
#include <string.h>

typedef struct {
    char  **paths;
    int     count;
    int     cap;
    char    dir[512];
    char    ext[16];   /* e.g. ".png" */
} collect_state_t;

static bool_t collect_cb(AXdirent const *e, void *userdata) {
    collect_state_t *s = (collect_state_t *)userdata;
    if (e->is_directory) return TRUE;

    /* case-insensitive extension check
     * (strcasecmp is POSIX; use _stricmp on Windows/MSVC) */
    size_t nlen = strlen(e->name);
    size_t elen = strlen(s->ext);
    if (nlen >= elen &&
        strcasecmp(e->name + nlen - elen, s->ext) == 0) {

        if (s->count >= s->cap) {
            s->cap = s->cap ? s->cap * 2 : 16;
            s->paths = realloc(s->paths, (size_t)s->cap * sizeof(char *));
        }
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", s->dir, e->name);
        /* strdup is POSIX; on MSVC use _strdup. Portable alternative: */
        size_t len = strlen(full) + 1;
        char *copy = (char *)malloc(len);
        if (copy) {
            memcpy(copy, full, len);
            s->paths[s->count++] = copy;
        }
    }
    return TRUE;
}

/* Usage */
collect_state_t state = { .ext = ".png" };
strncpy(state.dir, "/home/user/images", sizeof(state.dir) - 1);
axListDir(state.dir, collect_cb, &state);
/* state.paths[0..state.count-1] now hold full paths to every .png */
```

---

### 5 — Recursive `mkdir -p`

`axMkDir` creates only the final component.  For nested paths:

```c
#include "platform.h"
#include <string.h>

bool_t mkdir_p(const char *path) {
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            axMkDir(tmp);   /* ignore intermediate failures */
            *p = '/';
        }
    }
    return axMkDir(tmp);
}
```

---

## Platform support

| Function     | Windows | macOS | Linux (X11/Wayland) | QNX | WebGL |
|--------------|:-------:|:-----:|:-------------------:|:---:|:-----:|
| `axMkDir`    | ✓       | ✓     | ✓                   | ✓   | —     |
| `axListDir`  | ✓       | ✓     | ✓                   | ✓   | —     |
| `axGetCwd`   | ✓       | ✓     | ✓                   | ✓   | —     |

On **WebGL** all three functions return `FALSE` (the browser sandbox has no
direct filesystem access).  Check return values before using results.

---

## Headers to remove after porting

Once you have replaced all raw calls you can drop these platform-specific
includes from your source files:

| Header            | What it provided                       | Replace with |
|-------------------|----------------------------------------|--------------|
| `<dirent.h>`      | `DIR`, `struct dirent`, `opendir`, `readdir`, `closedir` | `"platform.h"` |
| `<unistd.h>`      | `getcwd` (POSIX)                       | `"platform.h"` |
| `<direct.h>`      | `_getcwd`, `_mkdir` (Windows/MSVC)     | `"platform.h"` |
| `<sys/stat.h>`    | `mkdir`, `stat`, `S_ISDIR` — if only used for dir listing | `"platform.h"` |

> `<sys/stat.h>` is still needed if you use `stat` to query individual file
> attributes beyond what `AXdirent` provides (e.g. permissions, inode numbers).
