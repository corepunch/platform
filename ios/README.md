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

`axCreateWindow` uses the scene's safe-area bounds. The backend owns its ES 3
context, Retina renderbuffer and depth/stencil framebuffer. Resizing reallocates
the drawable and posts `kEventWindowResized`; presentation retains the color
buffer for Orion's incremental repainting. `axGetSize` and pointer events are
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
`axSetTextInput` shows or hides the keyboard for an active text control.

The event queue accepts worker-thread posts and coalesces paint/resize events
by target. Timers run on the main run loop and are cancelled when their owner
is removed. Settings live in Application Support; the working directory is
Documents. Open imports a copy through the system document picker; native
save chooses a file in Documents. External directory picking, offscreen native
windows and joystick discovery are not implemented; rejected requests log to
stderr. The app may provide its own framework file picker for Documents.

Apple references: [OpenGL ES context and drawable](https://developer.apple.com/documentation/opengles)
and [coalesced touch input](https://developer.apple.com/documentation/uikit/getting-high-fidelity-input-with-coalesced-touches).
