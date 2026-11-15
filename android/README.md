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
  `axAndroidPostText()` (committed UTF-8) and `axAndroidPostKey()` (Android
  `KEYCODE_DEL`, `KEYCODE_FORWARD_DEL`, `KEYCODE_ENTER`), or any message
  through `axAndroidPostMessage()`; all are safe from any thread. Hardware
  editing keys (backspace, delete, enter, tab, arrows) arrive as key events.
- `axSetTextInput()` calls the activity's
  `public void showKeyboard(boolean visible, int type)` over JNI with type 0
  (plain text). It attaches the calling thread to the Java VM; the
  application detaches it before `android_main()` returns.
- Audio plays through AAudio (link `-laaudio`).
- `axAndroidSetInsets()` records system bar and soft keyboard insets, in
  points, reported by the activity's window-insets listener.
- The window comes and goes with the activity; the GL context outlives it.

Build: `make SDK=android ANDROID_NDK=/path/to/ndk ARCH=aarch64`.
