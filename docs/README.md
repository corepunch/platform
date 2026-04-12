# Axiom Platform

A lightweight C library that gives you a window, an OpenGL context, and a
stream of input events — nothing more, nothing less.  One header, one `make`,
works on Windows, macOS, Linux (Wayland or X11), and WebGL.

```c
#include "platform.h"
```

## Quick example

```c
#include "platform.h"

int main(void) {
    axInit();
    axCreateWindow("Hello", 800, 600, 0);

    struct AXmessage msg;
    while (1) {
        axWaitEvent(16);
        while (axPollEvent(&msg)) {
            if (msg.message == kEventWindowClosed) goto done;
        }
        axBeginPaint();
        /* glClear / draw calls here */
        axEndPaint();
    }
done:
    axShutdown();
}
```

## Build

```bash
make          # Linux / macOS / Windows (MinGW)
emmake make   # WebGL via Emscripten
```

## Articles

| | |
|---|---|
| [Creating a Window](creating-a-window.md) | Flags, resize, HiDPI, fullscreen |
| [Handling Input](handling-input.md) | Keyboard, mouse, modifiers |
| [The Event Loop](event-loop.md) | Poll, wait, timers, custom events |
| [OpenGL Rendering](opengl-rendering.md) | Paint loop, VSync, off-screen |
| [File Dialogs](file-dialogs.md) | Open, save, folder picker |
| [Joystick & Gamepad](joystick-gamepad.md) | Axes, buttons, XInput |
| [System Utilities](system-utilities.md) | Time, dark theme, directories |
