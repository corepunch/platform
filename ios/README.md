# iOS backend

Build the static platform library from this directory's parent:

```sh
make SDK=iphoneos ARCH=arm64 IOS_MIN=16.0 OUTDIR=/tmp/platform-ios
make SDK=iphonesimulator ARCH=arm64 IOS_MIN=16.0 OUTDIR=/tmp/platform-simulator
```

Link with UIKit, Foundation, CoreGraphics, QuartzCore, OpenGLES, Security and
UniformTypeIdentifiers. The other Unix platform services are included in the
archive. Include `platform.h`; `AX_PLATFORM_IOS` is detected from Apple's target
headers.

Call `axRunApplication(argc, argv, start, frame, stop)` from `main`. UIKit owns
the main thread: `start` initializes the app after its scene is laid out,
`frame` drains messages and renders, and `stop` releases the app's resources.
Use a scene manifest with `AXSceneDelegate`, one scene, an iPad device family,
a launch screen, and the orientations the app can lay out. Do not wrap the
frame callback in a desktop-style infinite event loop.

`axCreateWindow` covers the whole screen, under the notch and home indicator. The backend owns its ES 3
context, Retina renderbuffer and depth/stencil framebuffer. Resizing reallocates
the drawable and posts `kEventWindowResized`; presentation retains the color
buffer for standalone clients' incremental repainting. Embedded ORCA clears
each full frame and disables retained backing. `axGetSize` and pointer events are
in UIKit points; `axGetScaling` supplies the backing-pixel scale. Child-window
coordinate conversion remains the framework's responsibility.

`axGetMessage` is nonblocking on iOS. A synchronous framework modal loop must
call `axWaitMessage` so UIKit can process input. Native modal pickers pump the
run loop themselves. Display callbacks are guarded against nested reentry and
paused while inactive. Background transitions release an active pointer and
finish GL work before suspension.

Touch and coalesced Pencil samples become the standard AX pointer events;
additional fingers are ignored during a stroke. Two-finger pan becomes scroll
input. Hardware keys retain modifier flags for framework accelerators.
`axSetTextInput` shows or hides the keyboard for an active text control. While the
on-screen keyboard covers the bottom of the window, the backend slides the
whole app view up by the covered height, with the keyboard's own animation,
and back down when it hides. Pointer coordinates are unaffected.

When embedded by ORCA's scene delegate, the drawable fills the window even
behind the keyboard. `axIOSGetKeyboardInset` reports the bottom occlusion in
points for a docked keyboard spanning the window width, or zero for a hidden,
floating, or non-overlapping keyboard. Changes post resize and paint events
without changing the drawable size. The framework uses this inset to keep
controls above the keyboard while their backgrounds can extend behind it.
`axIOSGetSafeArea` reports the drawable's current safe insets in points,
independently of keyboard occlusion. `axIOSSetBackgroundColor`
matches the native backdrop and status-bar contrast to the rendered canvas.
`axIOSShowKeyboard` also accepts the keyboard layout and secure-entry trait;
changing traits while focused reloads the native input view.

The event queue accepts worker-thread posts and coalesces paint/resize events
by target. Timers run on the main run loop and are cancelled when their owner
is removed. Settings live in Application Support; the working directory is
Documents. Open starts the native document picker in Documents. Local documents
open directly; external selections are read with security-scoped access and file
coordination, then copied into Documents under a unique name. Native Save asks
for a filename, applies the filter extension and confirms replacement. Files dragged in
from other apps are copied into Documents under a unique name and posted as
`kEventDragDrop` at the drop point, one event per file. External directory picking, offscreen native
windows and joystick discovery are not implemented; rejected requests log to
stderr. Orion routes both Open and Save to these native dialogs.

Apple references: [OpenGL ES context and drawable](https://developer.apple.com/documentation/opengles)
and [coalesced touch input](https://developer.apple.com/documentation/uikit/getting-high-fidelity-input-with-coalesced-touches).
