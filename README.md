# platform
Platform layer for input/event handling and window management

## Building

This project includes a Makefile that automatically detects your platform (Linux or macOS) and builds the appropriate dynamic library.

### Prerequisites

**Linux:**
- GCC compiler
- Wayland development libraries (optional, for Wayland support):
  ```bash
  sudo apt-get install libwayland-dev libwayland-egl1 libxkbcommon-dev libegl-dev
  ```

**macOS:**
- Clang compiler (included with Xcode Command Line Tools)
- AppKit framework (included with macOS)

### Build Commands

```bash
# Build the dynamic library
make

# Clean build artifacts
make clean

# Install to /usr/local/lib (requires sudo on most systems)
sudo make install

# Uninstall from /usr/local/lib
sudo make uninstall

# Show available targets
make help
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
