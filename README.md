# platform
Platform layer for input/event handling and window management

[![Build](https://github.com/corepunch/platform/actions/workflows/build.yml/badge.svg)](https://github.com/corepunch/platform/actions/workflows/build.yml)

## Building

Simple Makefile that auto-detects your platform and builds the appropriate dynamic library using wildcards for source files.

### Prerequisites

**Linux:**
- GCC compiler
- Wayland development libraries (optional, for Wayland support):
  ```bash
  sudo apt-get install libwayland-dev libwayland-egl1-mesa libxkbcommon-dev libegl-dev libgl-dev
  ```

**macOS:**
- Clang compiler (included with Xcode Command Line Tools)
- AppKit framework (included with macOS)

### Build Commands

```bash
make          # Build the dynamic library
make clean    # Clean build artifacts
make install  # Install to /usr/local/lib (requires sudo)
```

### Output

- **Linux**: `libplatform.so`
- **macOS**: `libplatform.dylib`

The library exports platform-specific functions for window management, event handling, networking, and system utilities.

## Platform Support

- **Linux (Wayland)**: Full support with Wayland, EGL, and OpenGL
- **macOS**: Full support with AppKit and Cocoa frameworks
- **QNX**: Source files available (qnx/)

## Header

Include `platform.h` in your project to access platform types and APIs.
