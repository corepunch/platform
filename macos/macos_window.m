#include "macos_local.h"

#import <OpenGL/gl.h>

#define USE_SINGLE_WINDOW

#define MIN_WINDOW_WIDTH 256
#define MIN_WINDOW_HEIGHT 256

AX_API uint32_t _IOSurface = -1;

//#define API_TYPE_WINDOW "Window"

//// Private notifications that are reliably dispatched when a window is moved
/// by dragging its titlebar. / The object of the notification is the window
/// being dragged. / Available in macOS 10.12+
// static NSString* const NSWindowWillStartDraggingNotification =
// @"NSWindowWillStartDraggingNotification"; static NSString* const
// NSWindowDidEndDraggingNotification = @"NSWindowDidEndDraggingNotification";

/* Pixel-format attributes – rebuilt from flags each time axCreateWindow is
 * called.  Shared with axCreateSurface.
 *
 * Keep a safe default initializer so callers that create an off-screen surface
 * before creating a window still pass a valid, terminated attribute list to
 * CGLChoosePixelFormat.  These defaults match BuildPixelFormatAttributes(0). */
static NSOpenGLPixelFormatAttribute attributes[16] = {
  NSOpenGLPFAOpenGLProfile,
  NSOpenGLProfileVersion3_2Core,
  NSOpenGLPFADepthSize,
  24,
  NSOpenGLPFAStencilSize,
  8,
  NSOpenGLPFAColorSize,
  24,
  NSOpenGLPFAAlphaSize,
  8,
  NSOpenGLPFADoubleBuffer,
  NSOpenGLPFAAccelerated,
  NSOpenGLPFANoRecovery,
  0
};

static CGLPixelFormatAttribute surface_attributes[16] = {
  NSOpenGLPFAOpenGLProfile,
  NSOpenGLProfileVersion3_2Core,
  NSOpenGLPFAColorSize,
  24,
  NSOpenGLPFAAlphaSize,
  8,
  NSOpenGLPFADepthSize,
  24,
  NSOpenGLPFAStencilSize,
  8,
  NSOpenGLPFAAllowOfflineRenderers,
  0
};

static void
BuildPixelFormatAttributes(uint32_t flags)
{
  int i = 0;
  attributes[i++] = NSOpenGLPFAOpenGLProfile;
  attributes[i++] = NSOpenGLProfileVersion3_2Core;
  attributes[i++] = NSOpenGLPFADepthSize;
  attributes[i++] = 24;
  attributes[i++] = NSOpenGLPFAStencilSize;
  attributes[i++] = 8;
  attributes[i++] = NSOpenGLPFAColorSize;
  attributes[i++] = 24;
  attributes[i++] = NSOpenGLPFAAlphaSize;
  attributes[i++] = 8;
  
  if (flags & AX_WINDOW_DOUBLEBUFFER) {
    attributes[i++] = NSOpenGLPFADoubleBuffer;
  }
  
  attributes[i++] = NSOpenGLPFAAllowOfflineRenderers;
  attributes[i++] = NSOpenGLPFAAccelerated;
  attributes[i++] = NSOpenGLPFANoRecovery;
  attributes[i] = 0;
}

static void
BuildSurfacePixelFormatAttributes(void)
{
  int i = 0;
  surface_attributes[i++] = NSOpenGLPFAOpenGLProfile;
  surface_attributes[i++] = NSOpenGLProfileVersion3_2Core;
  surface_attributes[i++] = NSOpenGLPFAColorSize;
  surface_attributes[i++] = 24;
  surface_attributes[i++] = NSOpenGLPFAAlphaSize;
  surface_attributes[i++] = 8;
  surface_attributes[i++] = NSOpenGLPFADepthSize;
  surface_attributes[i++] = 24;
  surface_attributes[i++] = NSOpenGLPFAStencilSize;
  surface_attributes[i++] = 8;
  surface_attributes[i++] = NSOpenGLPFAAllowOfflineRenderers;
  surface_attributes[i] = 0;
}

struct wstate {
  NSWindow *Window;
  WindowDelegate *Delegate;
  id DarkModeObserver;
  NSOpenGLContext *windowCtx;
  CGLContextObj ctx;
  IOSurfaceRef surf;
  GLuint texnum, framebuffer, depth;
  GLuint width, height;
  GLfloat backingScale;
  uint32_t flags;
} wstate = {0};

void
axNotifySizeChanged(uint32_t width, uint32_t height)
{
  wstate.width = width;
  wstate.height = height;
  
  axPostMessageW(NULL, kEventWindowResized, MAKEDWORD(width, height), 0);
  axPostMessageW(NULL, kEventWindowPaint, MAKEDWORD(width, height), 0);
}

static NSRect
GetScreenFrame(uint32_t width)
{
  (void)width;
  NSScreen *mainScreen = [NSScreen mainScreen];
  if (mainScreen)
    return [mainScreen frame];
	NSArray   *screens    = [NSScreen screens];
  if ([screens count] > 0)
    return [[screens objectAtIndex:0] frame];
  return NSMakeRect(0, 0, MAX(width, (uint32_t)1024), 768);
//	NSUInteger numScreens = [screens count];
//	return [[screens objectAtIndex:(numScreens-1)] frame];
	//    FOR_LOOP(index, (int)numScreens) {
	//        NSScreen *screen = [screens objectAtIndex:index];
	//        NSRect    frame  = [screen visibleFrame];
	//        if (frame.size.width == width) {
	//            return frame;
	//        }
	//    }
	//    return [[NSScreen mainScreen] frame];
}

static NSRect
CenterOnScreen(uint32_t width, uint32_t height)
{
	width  = MAX(MIN_WINDOW_WIDTH, width);
	height = MAX(MIN_WINDOW_HEIGHT, height);
	NSRect screenRect = GetScreenFrame(width);
	float  x = (screenRect.size.width - width) / 2 + screenRect.origin.x;
	float  y = (screenRect.size.height - height) / 2 + screenRect.origin.y;
	return CGRectMake(x, y, width, height);
}

static bool IsDarkMode(NSWindow *window) {
  NSAppearance *appearance = [window effectiveAppearance];
  NSAppearanceName match = [appearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameDarkAqua, NSAppearanceNameAqua]];
  return [match isEqualToString:NSAppearanceNameDarkAqua];
}

static id ListenForDarkModeChanges(NSWindow *window) {
  return [[NSDistributedNotificationCenter defaultCenter]
          addObserverForName:@"AppleInterfaceThemeChangedNotification"
          object:nil
          queue:[NSOperationQueue mainQueue]
          usingBlock:^(NSNotification * _Nonnull note) {
    (void)note;
    if (IsDarkMode(window)) {
      NSLog(@"Switched to Dark Mode");
    } else {
      NSLog(@"Switched to Light Mode");
    }
  }];
}

static NSWindow *MakeWindow(NSRect windowRect, uint32_t flags) {
  int mask;
  if (flags & AX_WINDOW_BORDERLESS) {
    mask = NSWindowStyleMaskBorderless;
  } else {
    mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
           NSWindowStyleMaskMiniaturizable;
    /* Resizable by default (backward compat); RESIZABLE flag makes it explicit. */
    if ((flags == 0) || (flags & AX_WINDOW_RESIZABLE)) {
      mask |= NSWindowStyleMaskResizable;
    }
  }
  return [[NSWindow alloc] initWithContentRect:windowRect
                                     styleMask:mask
                                       backing:NSBackingStoreBuffered
                                         defer:NO];
}

static NSOpenGLPixelFormat *MakeOpenGLPixelFormat(void) {
  NSOpenGLPixelFormat *pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
  if (pf) return pf;

  NSOpenGLPixelFormatAttribute legacy[] = {
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy,
    NSOpenGLPFAAllowOfflineRenderers,
    NSOpenGLPFANoRecovery,
    0
  };
  return [[NSOpenGLPixelFormat alloc] initWithAttributes:legacy];
}

static NSOpenGLContext *MakeOpenGLContext(void) {
  NSOpenGLPixelFormat *pixelFormat = MakeOpenGLPixelFormat();
  if (!pixelFormat) {
    printf("Failed to create NSOpenGLPixelFormat\n");
    fflush(stdout);
    return nil;
  }
  NSOpenGLContext *context = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
  if (!context) {
    printf("Failed to create NSOpenGLContext\n");
    fflush(stdout);
  }
  [pixelFormat release];
  return context;
}

static void ConfigureOpenGLView(NSOpenGLView *openglView) {
  GLint            interval = 0;
  [openglView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
#ifdef ORION_FORCE_NON_RETINA
  [openglView setWantsBestResolutionOpenGLSurface:NO];
#else
  [openglView setWantsBestResolutionOpenGLSurface:YES];
#endif

  NSOpenGLContext *context = MakeOpenGLContext();
  [openglView setOpenGLContext:context];
  wstate.windowCtx = [context retain];
  [context update];
  [context makeCurrentContext];
  [context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];
}

bool_t
axCreateWindow(char const *title, uint32_t width, uint32_t height, uint32_t flags)
{
  /*
  NSOpenGLPixelFormatAttribute attrs[] = {
    NSOpenGLPFAAccelerated,
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
    0
  };
  
  NSOpenGLPixelFormat *pf =
  [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
  
  NSOpenGLContext *ctx =
  [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
  
  // No window/view needed
  [ctx makeCurrentContext];
  
  return TRUE;
  */
  if (wstate.Window) {
//    [g_window setContentSize:NSMakeSize(width, height)];
//    [g_window setFrameOrigin:CenterOnScreen(width, height).origin];
    [wstate.windowCtx makeCurrentContext];
    return TRUE;
  }

  wstate.flags = flags;

  BuildPixelFormatAttributes(flags);

	NSRect           windowRect  = CenterOnScreen(width, height);
	NSRect           viewRect    = CGRectMake(0, 0, width, height);
	NSOpenGLPixelFormat *pf      = MakeOpenGLPixelFormat();
	NSOpenGLView    *openGLView  = [[NSOpenGLView alloc] initWithFrame:viewRect pixelFormat:pf];
	NSString        *windowTitle = [[NSString alloc] initWithUTF8String:title];
	NSWindow        *window      = MakeWindow(windowRect, flags);
	WindowDelegate  *delegate    = [[WindowDelegate alloc] init];
  [pf release];

  [delegate setWindow:window];
  
	[window setFrameOrigin:windowRect.origin];
	[window setTitle:windowTitle];
  [window setReleasedWhenClosed:NO];
	[window setAcceptsMouseMovedEvents:YES];
	[window setContentView:openGLView];
	[window setDelegate:delegate];
	[window setInitialFirstResponder:openGLView];
	[window setContentSize:NSMakeSize(width, height)];
	[window display];
	[window setFrameOrigin:CenterOnScreen(width, height).origin];
	[window registerForDraggedTypes:[NSArray arrayWithObject:NSPasteboardTypeFileURL]];

  ConfigureOpenGLView(openGLView);

  if (!(flags & AX_WINDOW_HIDDEN)) {
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
  } else {
    [window orderOut:nil];
  }

  if (flags & AX_WINDOW_FULLSCREEN) {
    [window toggleFullScreen:nil];
  }

  wstate.DarkModeObserver = ListenForDarkModeChanges(window);
  
  if (wstate.width != width || wstate.height != height) {
    axNotifySizeChanged(width, height);
  }

  assert(!wstate.surf);
  
  wstate.Window = window;
  wstate.Delegate = delegate;
  
  if ([openGLView wantsBestResolutionOpenGLSurface]) {
    wstate.backingScale = [wstate.Window backingScaleFactor];
  } else {
    wstate.backingScale = 1;
  }

  [openGLView release];
  [windowTitle release];
    
	return TRUE;
}

void axShutdown(void) {
#ifndef USE_SINGLE_WINDOW
	NSWindow *window = hWnd->window;
  [window setContentView:nil];
  [window setDelegate:nil];
	[window close];
	[window release];
  free(hWnd);
#else
  if (wstate.Window) {
    NSWindow *window = wstate.Window;
    NSView *contentView = [window contentView];
    if ([contentView isKindOfClass:[NSOpenGLView class]]) {
      NSOpenGLContext *context = wstate.windowCtx ? wstate.windowCtx : [(NSOpenGLView *)contentView openGLContext];
      if ([NSOpenGLContext currentContext] == context) {
        [NSOpenGLContext clearCurrentContext];
      }
      [context clearDrawable];
      [(NSOpenGLView *)contentView setOpenGLContext:nil];
    }

    if (wstate.DarkModeObserver) {
      [[NSDistributedNotificationCenter defaultCenter] removeObserver:wstate.DarkModeObserver];
      wstate.DarkModeObserver = nil;
    }

    [window orderOut:nil];
    [window setInitialFirstResponder:nil];
    [window setContentView:nil];
    [window setDelegate:nil];
    [window close];
    [window release];
    wstate.Window = nil;
    if (wstate.windowCtx) {
      [wstate.windowCtx release];
      wstate.windowCtx = nil;
    }
  } else if (wstate.surf) {
    if (wstate.ctx && [NSOpenGLContext currentContext] == (NSOpenGLContext *)wstate.ctx) {
      [NSOpenGLContext clearCurrentContext];
    }
    if (wstate.framebuffer) glDeleteFramebuffers(1, &wstate.framebuffer);
    if (wstate.texnum) glDeleteTextures(1, &wstate.texnum);
    if (wstate.depth) glDeleteRenderbuffers(1, &wstate.depth);
    wstate.framebuffer = 0;
    wstate.texnum = 0;
    wstate.depth = 0;
    wstate.surf = NULL;
    if (wstate.ctx) {
      CGLReleaseContext(wstate.ctx);
      wstate.ctx = NULL;
    }
  }

  if (wstate.Delegate) {
    [wstate.Delegate release];
    wstate.Delegate = nil;
  }

  wstate.width = 0;
  wstate.height = 0;
  wstate.backingScale = 0;
  wstate.flags = 0;
#endif
}

float
axGetScaling(void)
{
  return MAX(wstate.backingScale, 1);
}

uint32_t axGetSize(struct AXsize * pSize) {
  if (pSize) {
    pSize->width = wstate.width;
    pSize->height = wstate.height;
  }
  return MAKEDWORD(MAX(MIN_WINDOW_WIDTH, wstate.width), MAX(MIN_WINDOW_HEIGHT, wstate.height));
}

bool_t axSetSize(uint32_t width, uint32_t height, bool_t centered) {
  if (wstate.width == width && wstate.height == height) {
    return TRUE;
  }
  if (wstate.Window) {
    wstate.width = MAX(MIN_WINDOW_WIDTH, width);
    wstate.height = MAX(MIN_WINDOW_HEIGHT, height);
    [wstate.Window setContentSize:NSMakeSize(wstate.width, wstate.height)];
    if (centered) {
      [wstate.Window setFrameOrigin:CenterOnScreen(wstate.width, wstate.height).origin];
      [wstate.Window makeKeyAndOrderFront:nil];
    }
    return TRUE;
  } else {
    return FALSE;
  }
}

void axBindFramebuffer(void) {
  if (wstate.Window) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  } else if (wstate.surf) {
    glBindFramebuffer(GL_FRAMEBUFFER, wstate.framebuffer);
  }
}

void axBeginPaint(void) {
  if (wstate.Window) {
    NSOpenGLView *view = [wstate.Window contentView];
    NSOpenGLContext *context = wstate.windowCtx ? wstate.windowCtx : [view openGLContext];
    [context update];
    [context makeCurrentContext];
    if (!glGetString(GL_VERSION)) {
      printf("OpenGL context not current in axBeginPaint: view=%p ctx=%p current=%p\n", view, context, [NSOpenGLContext currentContext]);
      fflush(stdout);
    }
  } else if (wstate.surf) {
    CGLSetCurrentContext(wstate.ctx);
  }
  axBindFramebuffer();
}

void axEndPaint(void) {
  if (wstate.Window) {
    if (!(wstate.flags & AX_WINDOW_DOUBLEBUFFER)) {
      glFlush();
    } else {
      [wstate.windowCtx flushBuffer];
    }
  }
}

void axHideWindow(void) {
  if (wstate.Window) [wstate.Window orderOut:nil];
}

void axMakeCurrentContext(void) {
  if (wstate.Window) {
    NSOpenGLContext *context = wstate.windowCtx;
    [context update];
    [context makeCurrentContext];
  } else if (wstate.surf) {
    CGLSetCurrentContext(wstate.ctx);
  }
}

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>

BOOL
IOSurface_Create(uint32_t w, uint32_t h)
{
  (void)CGLGetCurrentContext();
  glGenTextures(1, &wstate.texnum);
  glGenFramebuffers(1, &wstate.framebuffer);
  glGenRenderbuffers(1, &wstate.depth);
  glBindFramebuffer(GL_FRAMEBUFFER, wstate.framebuffer);
  glBindTexture(GL_TEXTURE_2D, wstate.texnum);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, wstate.texnum, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, wstate.depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, wstate.depth);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    NSLog(@"Can't create offscreen framebuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (wstate.framebuffer) glDeleteFramebuffers(1, &wstate.framebuffer);
    if (wstate.texnum) glDeleteTextures(1, &wstate.texnum);
    if (wstate.depth) glDeleteRenderbuffers(1, &wstate.depth);
    wstate.framebuffer = 0;
    wstate.texnum = 0;
    wstate.depth = 0;
    return NO;
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  
  wstate.surf = (IOSurfaceRef)1;
  wstate.width = w;
  wstate.height = h;
  wstate.backingScale = 1;

  axPostMessageW(NULL, kEventWindowPaint, MAKEDWORD(w, h), 0);
  return YES;
}

void
IOSurface_Release(uint32_t iosurface)
{
  if (iosurface) {
    IOSurfaceRef sharedSurface = IOSurfaceLookup(iosurface);
    if (sharedSurface) {
      CFRelease(sharedSurface);
    }
  }
}

bool_t
axCreateSurface(uint32_t width, uint32_t height)
{
  if (wstate.surf) {
    return TRUE;
  }
  BuildSurfacePixelFormatAttributes();
  CGLPixelFormatObj pix;
  GLint npix;
  if (CGLChoosePixelFormat(surface_attributes, &pix, &npix) != kCGLNoError || !pix) {
    NSLog(@"Failed to choose offscreen pixel format");
    return FALSE;
  }
  if (CGLCreateContext(pix, NULL, &wstate.ctx) != kCGLNoError || !wstate.ctx) {
    NSLog(@"Failed to create offscreen GL context");
    CGLDestroyPixelFormat(pix);
    return FALSE;
  }
  CGLDestroyPixelFormat(pix);
  CGLSetCurrentContext(wstate.ctx);
 
  assert(!wstate.Window);
  
  IOSurface_Create(width, height);
  
  return TRUE;
}

@implementation WindowDelegate {}

- (void) windowWillClose:(NSNotification *)aNotification {
  axPostMessageW(NULL, kEventWindowClosed, 0, 0);
}
//- (bool)validateMenuItem:(NSMenuItem *)menuItem {
//    NSLog(@"%@", menuItem.title);
//    return YES;
//}
- (void) windowDidResize:(NSNotification *)notification {
  uint32_t width = self.window.contentView.frame.size.width;
  uint32_t height = self.window.contentView.frame.size.height;
  //  axPostMessageW(NULL, kEventWindowResized, MAKEDWORD(width, height));
  axNotifySizeChanged(width, height);
}
-(void) windowDidChangeScreen:(NSNotification *)notification {
  axPostMessageW(NULL, kEventWindowChangedScreen, 0, 0);
}
- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender {
  if (([sender draggingSourceOperationMask] & NSDragOperationGeneric) == NSDragOperationGeneric) {
    return NSDragOperationGeneric;
  } else {
    return NSDragOperationNone; // no idea what to do with this, reject it.
  }
}
- (bool)performDragOperation:(id <NSDraggingInfo>)sender {
  NSPasteboard *pasteboard = [sender draggingPasteboard];
  NSArray *array = [pasteboard propertyListForType:@"NSFilenamesPboardType"];
  NSPoint mouse = [sender draggingLocation];
  float mouse_y = self.window.contentView.frame.size.height - mouse.y;
  for (NSString *path in array) {
    NSURL *fileURL = [NSURL fileURLWithPath:path];
    NSNumber *isAlias = nil;
    [fileURL getResourceValue:&isAlias
                       forKey:NSURLIsAliasFileKey
                        error:nil];
    /* If the URL is an alias, resolve it. */
    if ([isAlias boolValue]) {
      NSURLBookmarkResolutionOptions opts = NSURLBookmarkResolutionWithoutMounting | NSURLBookmarkResolutionWithoutUI;
      NSData *bookmark = [NSURL bookmarkDataWithContentsOfURL:fileURL
                                                        error:nil];
      if (bookmark != nil) {
        NSURL *resolvedURL = [NSURL URLByResolvingBookmarkData:bookmark
                                                       options:opts
                                                 relativeToURL:nil
                                           bookmarkDataIsStale:nil
                                                         error:nil];
        if (resolvedURL != nil) {
          fileURL = resolvedURL;
        }
      }
    }
    
    axNotifyFileDropEvent(fileURL.path.UTF8String, mouse.x, mouse_y);
  }
  return YES;
}

@end
