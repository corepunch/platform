<img width="1489" height="588" alt="7aaa527d-7867-418a-be61-5612fda46339" src="https://github.com/user-attachments/assets/98fa803c-bd18-4dc9-9a68-2b6d6fa77c8a" />

# Axiom Platform Abstraction Toolkit

[![Build](https://github.com/corepunch/platform/actions/workflows/build.yml/badge.svg)](https://github.com/corepunch/platform/actions/workflows/build.yml)

A lightweight, cross-platform C library providing a unified API for window management, input/event handling, and system utilities. Designed for applications that need direct control over windowing and input without heavy framework dependencies.

## Features

- **Cross-platform**: Native support for Windows (Win32/WGL via MinGW), Linux (Wayland or X11), macOS (AppKit/Cocoa), QNX, and WebGL (browser)
- **Window Management**: Create and manage windows with OpenGL/EGL context support
- **Event System**: Unified event handling for mouse, keyboard, and window events
- **Input Processing**: Comprehensive keyboard and mouse input with modifier support
- **Networking**: Portable TCP/UDP sockets, DNS resolution, TLS (Schannel/Secure Transport/OpenSSL), and non-blocking I/O
- **File Dialogs**: Native file open/save dialogs
- **System Integration**: Theme detection, directory paths, timing utilities
- **Minimal Dependencies**: Simple C API with no bloat

## Quickstart

Here's a minimal example showing how to create a window and process input events:

```c
#include "platform.h"
#include <stdio.h>

int main(void) {
    // Initialize the platform library
    axInit();
    
    // Create a window (800x600 pixels)
    if (!axCreateWindow("My Application", 800, 600, 0)) {
        fprintf(stderr, "Failed to create window\n");
        return 1;
    }
    
    // Main event loop
    struct AXmessage msg;
    int running = 1;
    
    while (running) {
        // Wait for events (with 16ms timeout for ~60 FPS)
        axWaitEvent(16);
        
        // Process all pending events
        while (axPollEvent(&msg)) {
            switch (msg.message) {
                case kEventWindowClosed:
                    running = 0;
                    break;
                    
                case kEventKeyDown:
                    printf("Key pressed: %d (modifiers: 0x%x)\n", 
                           msg.keyCode, msg.modflags);
                    
                    // Exit on ESC key
                    if (msg.keyCode == AX_KEY_ESCAPE) {
                        running = 0;
                    }
                    break;
                    
                case kEventLeftButtonDown:
                    printf("Mouse clicked at: %d, %d\n", msg.x, msg.y);
                    break;
                    
                case kEventMouseMoved:
                    // Mouse position in msg.x, msg.y
                    break;
                    
                case kEventWindowResized:
                    printf("Window resized\n");
                    break;
            }
        }
        
        // Render your frame here
        axBeginPaint();
        
        // ... OpenGL rendering calls ...
        
        axEndPaint();
    }
    
    // Cleanup
    axShutdown();
    return 0;
}
```

### Compiling the Example

On **Windows (MinGW/MSYS2)**:
```bash
gcc -o myapp main.c -L. -lplatform -lopengl32 -lgdi32 -luser32
```

On **Linux (X11)**:
```bash
gcc -o myapp main.c -L. -lplatform -lX11 -lEGL -lGL
```

On **Linux (Wayland)**:
```bash
gcc -o myapp main.c -L. -lplatform -lwayland-client -lwayland-egl -lEGL -lGL
```

On **macOS**:
```bash
clang -o myapp main.c -L. -lplatform -framework AppKit -framework OpenGL
```

On **WebGL (browser via Emscripten)**:
```bash
emcc -o myapp.js main.c -L. -lplatform -sUSE_WEBGL2=1 -sEXPORTED_RUNTIME_METHODS=ccall,cwrap
```

Make sure `libplatform.so` (Linux) or `libplatform.dylib` (macOS) is in your library path or current directory.

## API Overview

### Initialization and Cleanup

```c
void axInit(void);                    // Initialize the platform library
void axShutdown(void);                // Clean up and shutdown
```

### Window Management

```c
bool_t axCreateWindow(const char* title, uint32_t width, uint32_t height, uint32_t flags);
bool_t axCreateSurface(uint32_t width, uint32_t height);
bool_t axSetSize(uint32_t width, uint32_t height, bool_t centered);
uint32_t axGetSize(struct AXsize* size);
float axGetScaling(void);             // Get display scaling factor
```

### Event Processing

```c
int axPollEvent(struct AXmessage* msg);          // Poll for next event (non-blocking)
int axWaitEvent(longTime_t timeout_ms);           // Wait for events with timeout
void axPostMessageW(void* hobj, uint32_t event, uint32_t wparam, void* lparam);
```

### Event Types

The library supports comprehensive event handling through the `AX_Message` structure:

- **Mouse Events**: `kEventLeftButtonDown`, `kEventLeftButtonUp`, `kEventMouseMoved`, `kEventScrollWheel`
- **Keyboard Events**: `kEventKeyDown`, `kEventKeyUp`, `kEventChar`
- **Window Events**: `kEventWindowClosed`, `kEventWindowResized`, `kEventWindowPaint`
- **Drag & Drop**: `kEventDragDrop`, `kEventDragEnter`

### Rendering

```c
void axMakeCurrentContext(void);      // Make OpenGL context current
void axBeginPaint(void);              // Begin rendering frame
void axEndPaint(void);                // End rendering and swap buffers
void axBindFramebuffer(void);         // Bind default framebuffer
```

### File Dialogs

```c
bool_t axGetOpenFileName(AXopenfilename const* params);
bool_t axGetSaveFileName(AXopenfilename const* params);
bool_t axGetFolderName(AXopenfilename const* params);
```

### Networking

```c
bool_t axNetInit(void);                                           // Initialise networking subsystem
void   axNetShutdown(void);                                       // Release networking resources
int    axNetSocket(int af, int type);                             // Create a TCP or UDP socket
void   axNetClose(int sock);                                      // Close a socket
bool_t axNetConnect(int sock, const char* host, uint16_t port);   // Resolve host and connect
int    axNetSend(int sock, const void* buf, int len);             // Send data
int    axNetRecv(int sock, void* buf, int len);                   // Receive data
bool_t axNetWouldBlock(void);                                     // Test for EAGAIN/EWOULDBLOCK
bool_t axNetResolve(const char* host, char* out, int outlen);     // DNS lookup to IP string
int    axNetPoll(int sock, int events, int timeout_ms);           // Wait for I/O readiness
bool_t axNetBind(int sock, uint16_t port);                        // Bind to a local port
bool_t axNetListen(int sock, int backlog);                        // Mark socket as passive
int    axNetAccept(int sock);                                     // Accept an incoming connection

// TLS (Schannel on Windows, Secure Transport on macOS, OpenSSL on Linux)
AXtlsctx* axTlsConnect(int sock, const char* hostname);           // TLS handshake
void      axTlsClose(AXtlsctx* ctx);                             // Send close_notify and free
int       axTlsSend(AXtlsctx* ctx, const void* buf, int len);    // Send through TLS
int       axTlsRecv(AXtlsctx* ctx, void* buf, int len);          // Receive through TLS
```

See [`docs/networking.md`](docs/networking.md) for full examples.

### System Utilities

```c
longTime_t axGetMilliseconds(void);   // Get current time in milliseconds
void axSleep(longTime_t msec);        // Sleep for specified milliseconds
bool_t axIsDarkTheme(void);           // Check if dark theme is active
const char* axGetPlatform(void);      // Get platform name
const char* axSettingsDirectory(void); // Get settings directory path
const char* axShareDirectory(void);   // Get share directory path
const char* axLibDirectory(void);     // Get library directory path
```

### Logging

```c
bool_t axSetLogFile(const char *path); // Open or close the process log file
const char *axGetLogFile(void);        // Query current log file path
void axLog(const char *fmt, ...);      // Append one formatted log line
void axLogFlush(void);                 // Flush buffered log output
```

Example:

```c
char log_path[1024];
snprintf(log_path, sizeof(log_path), "%s/myapp.log", axSettingsDirectory());
if (axSetLogFile(log_path)) {
    axLog("app started on %s", axGetPlatform());
}
```

On WebGL builds, file logging is unavailable and `axSetLogFile("...")` returns `FALSE`.

### Key Codes

The library defines comprehensive key codes including:
- Standard ASCII characters
- Function keys (`AX_KEY_F1` - `AX_KEY_F12`)
- Arrow keys (`AX_KEY_UPARROW`, `AX_KEY_DOWNARROW`, etc.)
- Special keys (`AX_KEY_ENTER`, `AX_KEY_ESCAPE`, `AX_KEY_SPACE`, etc.)
- Mouse buttons (`AX_KEY_MOUSE1`, `AX_KEY_MOUSE2`, `AX_KEY_MOUSE3`)
- Modifier flags (`AX_MOD_SHIFT`, `AX_MOD_CTRL`, `AX_MOD_ALT`, `AX_MOD_CMD`)

See `platform.h` for the complete list of key codes and event definitions.

## Integrating as a Submodule

The recommended way to use platform in your own project is to add it as a git submodule and build it as part of your Makefile — in a single compiler pass, with no intermediate `.o` files. This keeps rebuilds fast and the integration simple.

### 1. Add the submodule

```bash
git submodule add https://github.com/corepunch/platform libs/platform
git submodule update --init --recursive
```

### 2. Add to your Makefile

```makefile
PLATFORM_DIR = libs/platform
PLATFORM_OUTDIR = build/lib

# Build platform into your output lib directory
platform:
	$(MAKE) -C $(PLATFORM_DIR) OUTDIR=$(abspath $(PLATFORM_OUTDIR))

# Link your app against it
myapp: platform
	$(CC) $(CFLAGS) -I$(PLATFORM_DIR) main.c \
	    -L$(PLATFORM_OUTDIR) -lplatform \
	    -Wl,-rpath,$(abspath $(PLATFORM_OUTDIR)) \
	    -o myapp
```

Internally, platform's Makefile compiles all sources in a **single pass** — no `.o` files are generated. All source files are collected with `find`, converted to `#include` directives, and piped directly to the compiler:

```makefile
$(FIND_SOURCES) | sed 's|.*|#include "&"|' | $(CC) $(CFLAGS) -x $(LANG) - $(LDFLAGS) -o $@
```

This means the entire library recompiles in one shot, which is extremely fast and requires no dependency tracking.

### 3. Clean up

```makefile
clean:
	$(MAKE) -C $(PLATFORM_DIR) clean
	rm -f myapp
```

For a real-world example using this pattern, see [corepunch/orca](https://github.com/corepunch/orca).

---

## Building

Simple Makefile that auto-detects your platform and builds the appropriate dynamic library using wildcards for source files.

### Prerequisites

**Windows (MinGW/MSYS2):**
- MinGW-w64 toolchain (GCC for Windows):
  ```bash
  # Install via MSYS2 (https://www.msys2.org/)
  pacman -S mingw-w64-x86_64-gcc make
  ```
- No extra libraries required — uses Win32, WGL, and Winsock2 (all included with Windows)

**WebGL (Emscripten):**
- Emscripten SDK (`emcc` compiler):
  ```bash
  # Install via emsdk (https://emscripten.org/docs/getting_started/downloads.html)
  git clone https://github.com/emscripten-core/emsdk.git
  cd emsdk && ./emsdk install latest && ./emsdk activate latest
  source ./emsdk_env.sh
  ```

**Linux:**
- GCC compiler
- Wayland development libraries (preferred, for Wayland support):
  ```bash
  sudo apt-get install libwayland-dev libwayland-egl1-mesa libxkbcommon-dev libegl-dev libgl-dev
  ```
- Or X11 development libraries (fallback, if Wayland is unavailable):
  ```bash
  sudo apt-get install libx11-dev libegl-dev libgl-dev
  ```
- Build order: **Wayland** (preferred) → **X11** (fallback) → **unix-only** (networking only)

**macOS:**
- Clang compiler (included with Xcode Command Line Tools)
- AppKit framework (included with macOS)

### Build Commands

```bash
make              # Build the dynamic library
emmake make       # Build for WebGL (requires Emscripten environment sourced)
make clean        # Clean build artifacts
make install      # Install to /usr/local/lib (requires sudo; Linux/macOS only)
```

### Output

- **Windows**: `libplatform.dll` (+ `libplatform.dll.a` import library)
- **Linux**: `libplatform.so`
- **macOS**: `libplatform.dylib`
- **WebGL**: `libplatform.wasm` (Emscripten side module)

The library exports platform-specific functions for window management, event handling, networking, and system utilities.

## Platform Support

- **Windows**: Full support via Win32 API and WGL — window creation, OpenGL rendering, native file dialogs, dark-theme detection, Winsock2 networking; built with MinGW
- **Linux (Wayland)**: Full support with Wayland, EGL, and OpenGL (preferred when available)
- **Linux (X11)**: Full support with Xlib, EGL, and OpenGL (fallback when Wayland is unavailable)
- **macOS**: Full support with AppKit and Cocoa frameworks
- **QNX**: Source files available (qnx/)
- **WebGL (browser)**: Full support via Emscripten — mouse, keyboard, scroll, and resize events; WebGL 1/2 rendering context

The Linux backend is selected automatically at build time:
1. If Wayland libraries are detected via `pkg-config`, the Wayland backend is used
2. If Wayland is unavailable but X11 libraries are detected, the X11 backend is used
3. If neither is available, a unix-only build is produced (networking only, no windowing)

## Header

Include `platform.h` in your project to access platform types and APIs.
