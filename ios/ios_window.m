#include "ios_local.h"
#import <UIKit/UIScreen.h>
#import <UIKit/UIWindowScene.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#include <math.h>

@interface AXIOSView : UIView <UIKeyInput>
@property(nonatomic, retain) EAGLContext *context;
@property(nonatomic, assign) GLuint framebuffer;
@property(nonatomic, assign) GLuint colorbuffer;
@property(nonatomic, assign) GLuint depthbuffer;
@property(nonatomic, assign) GLint pixelWidth;
@property(nonatomic, assign) GLint pixelHeight;
@property(nonatomic, retain) UITouch *primaryTouch;
@property(nonatomic, assign) CGPoint lastTouch;
@property(nonatomic, assign) CGPoint touchOrigin;
@property(nonatomic, assign) CGPoint scrollRemainder;
@property(nonatomic, assign) int touchMode;
- (void)destroyBuffers;
@end

@interface AXIOSController : UIViewController
@property(nonatomic, retain) AXIOSView *drawable;
@property(nonatomic, assign) CGRect keyboardFrame;
@end

static UIWindow *window;
static AXIOSController *controller;
static uint32_t width, height;

static uint32_t
PointParam(CGPoint p)
{
  return MAKEDWORD((int)CLAMP(p.x, 0, UINT16_MAX), (int)CLAMP(p.y, 0, UINT16_MAX));
}

static void
PostKey(uint32_t key, char const *text, size_t length)
{
  void *bytes = NULL;
  memcpy(&bytes, text, MIN(length, sizeof(bytes)));
  axPostMessageW(NULL, kEventKeyDown, key, bytes);
  axPostMessageW(NULL, kEventKeyUp, key, bytes);
}

@implementation AXIOSView

+ (Class)layerClass { return [CAEAGLLayer class]; }
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)hasText { return YES; }
- (UITextAutocorrectionType)autocorrectionType { return UITextAutocorrectionTypeNo; }
- (UITextAutocapitalizationType)autocapitalizationType { return UITextAutocapitalizationTypeNone; }
- (UITextSmartQuotesType)smartQuotesType { return UITextSmartQuotesTypeNo; }
- (UITextSmartDashesType)smartDashesType { return UITextSmartDashesTypeNo; }

- (void)insertText:(NSString *)text
{
  char const *utf8 = text.UTF8String;
  if (!utf8) return;
  for (size_t i = 0, length = strlen(utf8); i < length;) {
    unsigned char c = utf8[i];
    size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    n = MIN(n, length - i);
    if (c == '\n' || c == '\r') PostKey(AX_KEY_ENTER, "", 0);
    else {
      void *bytes = NULL;
      memcpy(&bytes, utf8 + i, n);
      axPostMessageW(NULL, kEventChar, 0, bytes);
    }
    i += n;
  }
}

- (void)deleteBackward { PostKey(AX_KEY_BACKSPACE, "", 0); }

- (void)destroyBuffers
{
  [EAGLContext setCurrentContext:self.context];
  if (_framebuffer) glDeleteFramebuffers(1, &_framebuffer);
  if (_colorbuffer) glDeleteRenderbuffers(1, &_colorbuffer);
  if (_depthbuffer) glDeleteRenderbuffers(1, &_depthbuffer);
  _framebuffer = _colorbuffer = _depthbuffer = 0;
  _pixelWidth = _pixelHeight = 0;
}

- (void)layoutSubviews
{
  [super layoutSubviews];
  if (!self.context || self.bounds.size.width <= 0 || self.bounds.size.height <= 0) return;
  GLint pw = (GLint)lround(self.bounds.size.width * self.contentScaleFactor);
  GLint ph = (GLint)lround(self.bounds.size.height * self.contentScaleFactor);
  if (_framebuffer && pw == _pixelWidth && ph == _pixelHeight) return;
  [self destroyBuffers];
  glGenFramebuffers(1, &_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
  glGenRenderbuffers(1, &_colorbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, _colorbuffer);
  if (![self.context renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)self.layer]) {
    [self destroyBuffers];
    return;
  }
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_pixelWidth);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_pixelHeight);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorbuffer);
  glGenRenderbuffers(1, &_depthbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, _depthbuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _pixelWidth, _pixelHeight);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthbuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depthbuffer);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    [self destroyBuffers];
    return;
  }
  width = (uint32_t)lround(self.bounds.size.width);
  height = (uint32_t)lround(self.bounds.size.height);
  axPostMessageW(NULL, kEventWindowResized, MAKEDWORD(width, height), NULL);
  axPostMessageW(NULL, kEventWindowPaint, MAKEDWORD(width, height), NULL);
}

- (void)postTouch:(UITouch *)touch event:(uint32_t)event
{
  CGPoint p = [touch locationInView:self];
  uint32_t delta = MAKEDWORD((int)(p.x - _lastTouch.x), (int)(p.y - _lastTouch.y));
  axPostMessageW(NULL, event, PointParam(p), (void *)(uintptr_t)delta);
  _lastTouch = p;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  (void)event;
  if (_primaryTouch) return;
  self.primaryTouch = touches.anyObject;
  _lastTouch = [_primaryTouch locationInView:self];
  _touchOrigin = _lastTouch;
  _scrollRemainder = CGPointZero;
  _touchMode = 0;
  [self postTouch:_primaryTouch event:kEventMouseMoved];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  (void)event;
  if (![touches containsObject:_primaryTouch]) return;
  CGPoint p = [_primaryTouch locationInView:self];
  if (!_touchMode) {
    CGFloat dx = p.x - _touchOrigin.x, dy = p.y - _touchOrigin.y;
    if (hypot(dx, dy) < 8) return;
    /* Defer the press until direction is known so swipes cannot click buttons. */
    _touchMode = fabs(dy) >= fabs(dx) ? 2 : 1;
    if (_touchMode == 1)
      axPostMessageW(NULL, kEventLeftButtonDown, PointParam(_touchOrigin), NULL);
  }
  if (_touchMode == 1) {
    [self postTouch:_primaryTouch event:kEventLeftButtonDragged];
    return;
  }
  _scrollRemainder.x += p.x - _lastTouch.x;
  _scrollRemainder.y += p.y - _lastTouch.y;
  int dx = (int)lround(_scrollRemainder.x), dy = (int)lround(_scrollRemainder.y);
  _scrollRemainder.x -= dx;
  _scrollRemainder.y -= dy;
  _lastTouch = p;
  if (dx || dy)
    axPostMessageW(NULL, kEventScrollWheel, PointParam(_touchOrigin),
                   (void *)(uintptr_t)MAKEDWORD(dx, dy));
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  (void)event;
  if (![touches containsObject:_primaryTouch]) return;
  if (!_touchMode) {
    [self postTouch:_primaryTouch event:kEventLeftButtonDown];
    if (_primaryTouch.tapCount == 2) [self postTouch:_primaryTouch event:kEventLeftDoubleClick];
  }
  if (_touchMode != 2) [self postTouch:_primaryTouch event:kEventLeftButtonUp];
  self.primaryTouch = nil;
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
  (void)event;
  if (![touches containsObject:_primaryTouch]) return;
  if (_touchMode == 1)
    axPostMessageW(NULL, kEventLeftButtonUp, MAKEDWORD(UINT16_MAX, UINT16_MAX), NULL);
  self.primaryTouch = nil;
}

- (void)dealloc
{
  [self destroyBuffers];
  [_primaryTouch release];
  [_context release];
  [super dealloc];
}
@end

@implementation AXIOSController

- (void)viewDidLoad
{
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.blackColor;
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:)
                                               name:UIKeyboardWillChangeFrameNotification object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(keyboardChanged:)
                                               name:UIKeyboardWillHideNotification object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationInactive:)
                                               name:UIApplicationWillResignActiveNotification object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationActive:)
                                               name:UIApplicationDidBecomeActiveNotification object:nil];
}

- (void)viewDidLayoutSubviews
{
  [super viewDidLayoutSubviews];
  CGRect frame = UIEdgeInsetsInsetRect(self.view.bounds, self.view.safeAreaInsets);
  if (!CGRectIsEmpty(_keyboardFrame)) {
    CGRect keyboard = [self.view convertRect:_keyboardFrame fromView:nil];
    CGRect overlap = CGRectIntersection(frame, keyboard);
    if (!CGRectIsNull(overlap) && CGRectGetMaxY(keyboard) >= CGRectGetMaxY(frame))
      frame.size.height = MAX(0, CGRectGetMinY(overlap) - CGRectGetMinY(frame));
  }
  self.drawable.frame = frame;
  [self.drawable setNeedsLayout];
  [self.drawable layoutIfNeeded];
}

- (void)keyboardChanged:(NSNotification *)notification
{
  self.keyboardFrame = [notification.name isEqualToString:UIKeyboardWillHideNotification]
                    ? CGRectZero : [notification.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
  [self.view setNeedsLayout];
  [self.view layoutIfNeeded];
}

- (void)applicationInactive:(NSNotification *)notification
{
  (void)notification;
  if (_drawable.primaryTouch) {
    if (_drawable.touchMode == 1)
      axPostMessageW(NULL, kEventLeftButtonUp, MAKEDWORD(UINT16_MAX, UINT16_MAX), NULL);
    _drawable.primaryTouch = nil;
  }
  axPostMessageW(NULL, kEventKillFocus, 0, NULL);
}

- (void)applicationActive:(NSNotification *)notification
{
  (void)notification;
  axPostMessageW(NULL, kEventSetFocus, 0, NULL);
  axPostMessageW(NULL, kEventWindowPaint, MAKEDWORD(width, height), NULL);
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [_drawable release];
  [super dealloc];
}
@end

UIWindow *axIOSGetWindow(void) { return window; }

void
axIOSShowKeyboard(bool_t visible)
{
  AXIOSView *view = controller.drawable;
  if (visible && !view.isFirstResponder &&
      UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
    [view becomeFirstResponder];
  else if (!visible && view.isFirstResponder) [view resignFirstResponder];
}

bool_t
axCreateWindow(char const *title, uint32_t requested_width, uint32_t requested_height, uint32_t flags)
{
  (void)requested_width;
  (void)requested_height;
  if (window) return TRUE;
  if (![NSThread isMainThread]) return FALSE;
  UIWindowScene *windowScene = nil;
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:[UIWindowScene class]]) continue;
    windowScene = (UIWindowScene *)scene;
    if (scene.activationState == UISceneActivationStateForegroundActive ||
        scene.activationState == UISceneActivationStateForegroundInactive) break;
  }
  if (!windowScene) return FALSE;
  EAGLContext *context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
  if (!context) return FALSE;
  window = [[UIWindow alloc] initWithWindowScene:windowScene];
  window.frame = windowScene.coordinateSpace.bounds;
  controller = [[AXIOSController alloc] init];
  controller.title = title ? [NSString stringWithUTF8String:title] : @"ORCA";
  AXIOSView *view = [[AXIOSView alloc] initWithFrame:window.bounds];
  view.context = context;
  [context release];
  view.contentScaleFactor = window.screen.scale;
  view.multipleTouchEnabled = NO;
  CAEAGLLayer *layer = (CAEAGLLayer *)view.layer;
  layer.opaque = YES;
  layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking: @NO,
                               kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8};
  controller.drawable = view;
  [controller.view addSubview:view];
  [view release];
  window.rootViewController = controller;
  if (!(flags & AX_WINDOW_HIDDEN)) [window makeKeyAndVisible];
  [controller.view setNeedsLayout];
  [controller.view layoutIfNeeded];
  if (!controller.drawable.framebuffer) {
    axShutdown();
    return FALSE;
  }
  axMakeCurrentContext();
  return TRUE;
}

void
axShutdown(void)
{
  axIOSShowKeyboard(FALSE);
  [controller.drawable destroyBuffers];
  window.hidden = YES;
  window.rootViewController = nil;
  [controller release];
  controller = nil;
  [window release];
  window = nil;
  [EAGLContext setCurrentContext:nil];
  width = height = 0;
  axIOSShutdownEvents();
}

bool_t axCreateSurface(uint32_t w, uint32_t h) { (void)w; (void)h; return FALSE; }
float axGetScaling(void) { return controller.drawable ? controller.drawable.contentScaleFactor : 1; }

bool_t
axSetSize(uint32_t w, uint32_t h, bool_t centered)
{
  (void)centered;
  return w == width && h == height;
}

uint32_t
axGetSize(struct AXsize *size)
{
  if (size) *size = (struct AXsize){width, height};
  return MAKEDWORD(width, height);
}

void axMakeCurrentContext(void) { [EAGLContext setCurrentContext:controller.drawable.context]; }
void axBindFramebuffer(void) { glBindFramebuffer(GL_FRAMEBUFFER, controller.drawable.framebuffer); }

void
axBeginPaint(void)
{
  axMakeCurrentContext();
  axBindFramebuffer();
}

void
axEndPaint(void)
{
  AXIOSView *view = controller.drawable;
  if (!view.framebuffer || UIApplication.sharedApplication.applicationState == UIApplicationStateBackground) return;
  glBindRenderbuffer(GL_RENDERBUFFER, view.colorbuffer);
  [view.context presentRenderbuffer:GL_RENDERBUFFER];
}

bool_t axSetSwapInterval(int interval) { return interval == 0 || interval == 1; }
