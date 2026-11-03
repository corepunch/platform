# Android backend

NativeActivity backend for OpenGL ES 3 through EGL. The application provides
`android_main()`, calls `axAndroidSetApp()` with the glue's `android_app`
before `axInit()`, and then uses the ordinary blocking message API:
`axWaitMessage()` pumps the activity's `ALooper` (lifecycle commands, input,
timers and a cross-thread inbox).

- Sizes and pointer coordinates are density-independent points (160 dpi = 1x);
  `axGetScaling()` returns pixels per point.
- One finger drives the left-button stream (down, dragged, up, cancel), with
  double-tap detection; scroll axes post `kEventScrollWheel`.
- Text input comes from the application's Java input view through
  `axAndroidPostMessage()`, which is safe from any thread. Hardware editing
  keys (backspace, delete, enter, tab, arrows) arrive as key events.
- `axAndroidSetInsets()` records system bar and soft keyboard insets, in
  points, reported by the activity's window-insets listener.
- The window comes and goes with the activity; the GL context outlives it.

Build: `make SDK=android ANDROID_NDK=/path/to/ndk ARCH=aarch64`.
