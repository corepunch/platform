#include "ios_local.h"
#include <unistd.h>
#include <math.h>

UIWindow *ios_window;
AXView *ios_view;
EAGLContext *ios_context;
CADisplayLink *ios_display_link;
static GLuint ios_framebuffer, ios_color, ios_depth;
static int ios_width, ios_height, ios_pixel_w, ios_pixel_h;
static float ios_scale = 1;
static bool_t (*ios_start)(int, char **);
static void (*ios_frame)(void), (*ios_stop)(void);
static int ios_argc;
static char **ios_argv;
static bool_t ios_started, ios_in_frame;

static void ios_touch(UITouch *touch, uint32_t event) {
  CGPoint p = [touch locationInView:ios_view];
  CGPoint old = [touch previousLocationInView:ios_view];
  if (event != kEventLeftButtonDragged)
    IOS_TRACE("touch view=%p event=%u x=%.1f y=%.1f pencil=%d", (__bridge void *)ios_view, event, p.x, p.y, touch.type == UITouchTypePencil);
  axPostMessageW(NULL, event, MAKEDWORD((int)p.x, (int)p.y),
    event == kEventLeftButtonDragged ? (void *)(intptr_t)MAKEDWORD((int)(p.x - old.x), (int)(p.y - old.y)) : NULL);
}

static bool_t ios_key(UIPress *press, uint32_t event, bool_t text_input) {
  UIKey *key = press.key;
  if (!key) return FALSE;
  uint32_t modifiers = 0, code = 0;
  if (key.modifierFlags & UIKeyModifierShift) modifiers |= AX_MOD_SHIFT;
  if (key.modifierFlags & UIKeyModifierControl) modifiers |= AX_MOD_CTRL;
  if (key.modifierFlags & UIKeyModifierCommand) modifiers |= AX_MOD_CMD;
  if (key.modifierFlags & UIKeyModifierAlternate) modifiers |= AX_MOD_ALT;
  switch (key.keyCode) {
    case UIKeyboardHIDUsageKeyboardReturnOrEnter: code = AX_KEY_ENTER;      break;
    case UIKeyboardHIDUsageKeyboardEscape:        code = AX_KEY_ESCAPE;     break;
    case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: code = AX_KEY_BACKSPACE; break;
    case UIKeyboardHIDUsageKeyboardTab:           code = AX_KEY_TAB;        break;
    case UIKeyboardHIDUsageKeyboardLeftArrow:     code = AX_KEY_LEFTARROW;  break;
    case UIKeyboardHIDUsageKeyboardRightArrow:    code = AX_KEY_RIGHTARROW; break;
    case UIKeyboardHIDUsageKeyboardUpArrow:       code = AX_KEY_UPARROW;    break;
    case UIKeyboardHIDUsageKeyboardDownArrow:     code = AX_KEY_DOWNARROW;  break;
    default:
      if (text_input && !(modifiers & (AX_MOD_CMD | AX_MOD_CTRL | AX_MOD_ALT))) return FALSE;
      if (key.charactersIgnoringModifiers.length) code = [key.charactersIgnoringModifiers characterAtIndex:0];
      break;
  }
  axPostMessageW(NULL, kEventModifiersChanged, modifiers, NULL);
  if (!code) return TRUE;
  void *chars = NULL;
  if (event == kEventKeyDown && !(modifiers & (AX_MOD_CMD | AX_MOD_CTRL | AX_MOD_ALT)))
    memcpy(&chars, key.characters.UTF8String, MIN(strlen(key.characters.UTF8String), sizeof(chars)));
  IOS_TRACE("key event=%u code=%u modifiers=%u", event, code, modifiers);
  axPostMessageW(NULL, event, code | modifiers, chars);
  return TRUE;
}

@implementation AXView
+ (Class)layerClass { return [CAEAGLLayer class]; }
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)hasText { return YES; }
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) if (!ios_key(press, kEventKeyDown, TRUE)) [super pressesBegan:[NSSet setWithObject:press] withEvent:event];
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) if (!ios_key(press, kEventKeyUp, TRUE)) [super pressesEnded:[NSSet setWithObject:press] withEvent:event];
}
- (void)insertText:(NSString *)text {
  IOS_TRACE("text input length=%lu", (unsigned long)text.length);
  for (NSUInteger i = 0; i < text.length; i++) {
    NSString *part = [text substringWithRange:NSMakeRange(i, 1)];
    void *chars = NULL;
    memcpy(&chars, part.UTF8String, MIN(strlen(part.UTF8String), sizeof(chars)));
    uint32_t key = [text characterAtIndex:i];
    if (key == '\n') key = AX_KEY_ENTER;
    axPostMessageW(NULL, kEventKeyDown, key, chars);
    axPostMessageW(NULL, kEventKeyUp, key, NULL);
  }
}
- (void)deleteBackward {
  IOS_TRACE("delete backward");
  axPostMessageW(NULL, kEventKeyDown, AX_KEY_BACKSPACE, NULL);
  axPostMessageW(NULL, kEventKeyUp, AX_KEY_BACKSPACE, NULL);
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (self.gesture_first) return;
  if (self.active_touch.type != UITouchTypePencil) {
    NSMutableArray<UITouch *> *fingers = [NSMutableArray new];
    for (UITouch *t in event.allTouches)
      if (t.type == UITouchTypeDirect && t.phase != UITouchPhaseEnded && t.phase != UITouchPhaseCancelled)
        [fingers addObject:t];
    if (fingers.count >= 2) {
      [self cancel_touch];
      self.gesture_first = fingers[0]; self.gesture_second = fingers[1];
      CGPoint a = [self.gesture_first locationInView:self], b = [self.gesture_second locationInView:self];
      self.gesture_center = CGPointMake((a.x + b.x) / 2, (a.y + b.y) / 2);
      self.gesture_vector = CGPointMake(b.x - a.x, b.y - a.y);
      IOS_TRACE("gesture begin x=%.1f y=%.1f", self.gesture_center.x, self.gesture_center.y);
      ios_post_gesture((ax_gesture_t){AX_GESTURE_BEGIN, self.gesture_center.x, self.gesture_center.y,
                                    self.gesture_center.x, self.gesture_center.y, 1, 0});
      return;
    }
  }
  UITouch *touch = touches.anyObject;
  for (UITouch *candidate in touches) if (candidate.type == UITouchTypePencil) touch = candidate;
  if (self.active_touch) {
    if (touch.type != UITouchTypePencil || self.active_touch.type == UITouchTypePencil) return;
    [self cancel_touch];
  }
  self.active_touch = touch;
  ios_touch(touch, kEventLeftButtonDown);
  if (touch.tapCount == 2) ios_touch(touch, kEventLeftDoubleClick);
}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if (self.gesture_first) {
    CGPoint a = [self.gesture_first locationInView:self], b = [self.gesture_second locationInView:self];
    CGPoint center = CGPointMake((a.x + b.x) / 2, (a.y + b.y) / 2);
    CGPoint vector = CGPointMake(b.x - a.x, b.y - a.y), old = self.gesture_vector;
    float length = hypotf(vector.x, vector.y), previous = hypotf(old.x, old.y);
    float scale = previous > 1 && length > 1 ? length / previous : 1;
    float angle = previous > 1 && length > 1 ? atan2f(old.x * vector.y - old.y * vector.x,
                                                                  old.x * vector.x + old.y * vector.y) : 0;
    ios_post_gesture((ax_gesture_t){AX_GESTURE_UPDATE, center.x, center.y,
                                  self.gesture_center.x, self.gesture_center.y, scale, angle});
    self.gesture_center = center; self.gesture_vector = vector;
    return;
  }
  if (![touches containsObject:self.active_touch]) return;
  for (UITouch *touch in [event coalescedTouchesForTouch:self.active_touch] ?: @[self.active_touch])
    ios_touch(touch, kEventLeftButtonDragged);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if ([touches containsObject:self.gesture_first] || [touches containsObject:self.gesture_second]) [self end_gesture:NO];
  if ([touches containsObject:self.active_touch]) {
    ios_touch(self.active_touch, kEventLeftButtonUp);
    self.active_touch = nil;
  }
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if ([touches containsObject:self.gesture_first] || [touches containsObject:self.gesture_second]) [self end_gesture:YES];
  if ([touches containsObject:self.active_touch]) [self cancel_touch];
}
- (void)cancel_touch {
  if (self.active_touch) ios_touch(self.active_touch, kEventPointerCancel);
  self.active_touch = nil;
}
- (void)end_gesture:(BOOL)cancelled {
  if (!self.gesture_first) return;
  IOS_TRACE("gesture end cancelled=%d", cancelled);
  ios_post_gesture((ax_gesture_t){cancelled ? AX_GESTURE_CANCEL : AX_GESTURE_END,
    self.gesture_center.x, self.gesture_center.y, self.gesture_center.x, self.gesture_center.y, 1, 0});
  self.gesture_first = nil; self.gesture_second = nil;
}
- (void)layoutSubviews { [super layoutSubviews]; if (ios_context) ios_resize_surface(); }
- (void)hover:(UIHoverGestureRecognizer *)gesture {
  CGPoint p = [gesture locationInView:self];
  axPostMessageW(NULL, kEventMouseMoved, MAKEDWORD((int)p.x, (int)p.y), NULL);
}
- (void)scroll:(UIPanGestureRecognizer *)gesture {
  CGPoint p = [gesture locationInView:self], delta = [gesture translationInView:self];
  if (gesture.state == UIGestureRecognizerStateChanged) {
    axPostMessageW(NULL, kEventScrollWheel, MAKEDWORD((int)p.x, (int)p.y),
                   (void *)(intptr_t)MAKEDWORD((int)delta.x, (int)delta.y));
    [gesture setTranslation:CGPointZero inView:self];
  }
}
@end

@interface AXController : UIViewController
@end
@implementation AXController
- (BOOL)canBecomeFirstResponder { return YES; }
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) ios_key(press, kEventKeyDown, FALSE);
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) ios_key(press, kEventKeyUp, FALSE);
}
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.blackColor;
  ios_view = [[AXView alloc] initWithFrame:self.view.bounds];
  ios_view.multipleTouchEnabled = YES;
  ios_view.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:ios_view];
  [NSLayoutConstraint activateConstraints:@[
    [ios_view.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [ios_view.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
    [ios_view.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [ios_view.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]]];
  [ios_view addGestureRecognizer:[[UIHoverGestureRecognizer alloc] initWithTarget:ios_view action:@selector(hover:)]];
  UIPanGestureRecognizer *scroll = [[UIPanGestureRecognizer alloc] initWithTarget:ios_view action:@selector(scroll:)];
  scroll.minimumNumberOfTouches = 2;
  scroll.allowedTouchTypes = @[@(UITouchTypeIndirectPointer)];
  scroll.allowedScrollTypesMask = UIScrollTypeMaskAll;
  [ios_view addGestureRecognizer:scroll];
}
- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  if (ios_started) return;
  [self becomeFirstResponder];
  [self.view layoutIfNeeded];
  ios_started = ios_start(ios_argc, ios_argv);
  if (!ios_started) { IOS_TRACE("application initialization failed"); return; }
  ios_display_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(frame:)];
  [ios_display_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
- (void)frame:(CADisplayLink *)link {
  if (!ios_started || ios_in_frame || UIApplication.sharedApplication.applicationState != UIApplicationStateActive) return;
  ios_in_frame = TRUE;
  ios_frame();
  ios_in_frame = FALSE;
}
@end

@interface AXSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation AXSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
  ios_window = self.window;
  self.window.rootViewController = [AXController new];
  [self.window makeKeyAndVisible];
}
- (void)sceneWillResignActive:(UIScene *)scene {
  IOS_TRACE("scene inactive");
  [ios_view cancel_touch];
  [ios_view end_gesture:YES];
  axPostMessageW(NULL, kEventKillFocus, 0, NULL);
  ios_display_link.paused = YES;
  if (ios_context) { [EAGLContext setCurrentContext:ios_context]; glFinish(); }
}
- (void)sceneDidBecomeActive:(UIScene *)scene {
  IOS_TRACE("scene active");
  ios_display_link.paused = NO;
  axPostMessageW(NULL, kEventWindowPaint, axGetSize(NULL), NULL);
}
@end

@interface AXApplicationDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AXApplicationDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Orion" sessionRole:session.role];
  config.delegateClass = AXSceneDelegate.class;
  return config;
}
- (void)applicationWillTerminate:(UIApplication *)application { if (ios_started && ios_stop) ios_stop(); }
@end

int axRunApplication(int argc, char **argv, bool_t (*start)(int, char **), void (*frame)(void), void (*stop)(void)) {
  if (!start || !frame) { IOS_TRACE("application callbacks missing"); return 1; }
  ios_argc = argc; ios_argv = argv; ios_start = start; ios_frame = frame; ios_stop = stop;
  @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(AXApplicationDelegate.class)); }
}

void ios_resize_surface(void) {
  int w = (int)ios_view.bounds.size.width, h = (int)ios_view.bounds.size.height;
  int pw = (int)(w * ios_scale), ph = (int)(h * ios_scale);
  if (!w || !h || (pw == ios_pixel_w && ph == ios_pixel_h)) return;
  [EAGLContext setCurrentContext:ios_context];
  glBindRenderbuffer(GL_RENDERBUFFER, ios_color);
  if (![ios_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)ios_view.layer]) {
    IOS_TRACE("drawable storage failed size=%dx%d", pw, ph); return;
  }
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &ios_pixel_w);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &ios_pixel_h);
  glBindRenderbuffer(GL_RENDERBUFFER, ios_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, ios_pixel_w, ios_pixel_h);
  glBindFramebuffer(GL_FRAMEBUFFER, ios_framebuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ios_color);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ios_depth);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ios_depth);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) { IOS_TRACE("incomplete framebuffer status=0x%x", status); return; }
  ios_width = w; ios_height = h;
  IOS_TRACE("resize view=%p points=%dx%d pixels=%dx%d", (__bridge void *)ios_view, w, h, ios_pixel_w, ios_pixel_h);
  axPostMessageW(NULL, kEventWindowResized, MAKEDWORD(w, h), NULL);
  axPostMessageW(NULL, kEventWindowPaint, MAKEDWORD(w, h), NULL);
}

bool_t axCreateWindow(const char *title, uint32_t w, uint32_t h, uint32_t flags) {
  if (!ios_view) { IOS_TRACE("create window requires axRunApplication"); return FALSE; }
  ios_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
  if (!ios_context) { IOS_TRACE("OpenGL ES 3 context creation failed"); return FALSE; }
  [EAGLContext setCurrentContext:ios_context];
  ios_scale = flags & AX_WINDOW_HIGHDPI ? ios_window.screen.scale : 1;
  ios_view.contentScaleFactor = ios_scale;
  CAEAGLLayer *layer = (CAEAGLLayer *)ios_view.layer;
  layer.opaque = YES;
  layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking: @YES, kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8};
  glGenFramebuffers(1, &ios_framebuffer);
  glGenRenderbuffers(1, &ios_color);
  glGenRenderbuffers(1, &ios_depth);
  ios_resize_surface();
  IOS_TRACE("create window title=%s", title);
  return ios_width > 0 && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
void axInit(void) {
  NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  if (chdir(documents.fileSystemRepresentation)) IOS_TRACE("could not select Documents working directory");
}
void axShutdown(void) {
  [ios_display_link invalidate]; ios_display_link = nil;
  ios_cancel_timers();
  [EAGLContext setCurrentContext:ios_context];
  glDeleteFramebuffers(1, &ios_framebuffer); glDeleteRenderbuffers(1, &ios_color); glDeleteRenderbuffers(1, &ios_depth);
  ios_framebuffer = ios_color = ios_depth = 0;
  [EAGLContext setCurrentContext:nil]; ios_context = nil;
  ios_width = ios_height = ios_pixel_w = ios_pixel_h = 0;
}
float axGetScaling(void) { return ios_scale; }
uint32_t axGetSize(struct AXsize *size) {
  if (size) { size->width = ios_width; size->height = ios_height; }
  return MAKEDWORD(ios_width, ios_height);
}
void axMakeCurrentContext(void) { [EAGLContext setCurrentContext:ios_context]; }
void axBindFramebuffer(void) { glBindFramebuffer(GL_FRAMEBUFFER, ios_framebuffer); }
void axBeginPaint(void) { axMakeCurrentContext(); axBindFramebuffer(); }
void axEndPaint(void) {
  glBindRenderbuffer(GL_RENDERBUFFER, ios_color);
  if (![ios_context presentRenderbuffer:GL_RENDERBUFFER]) IOS_TRACE("present failed");
}
bool_t axSetSize(uint32_t w, uint32_t h, bool_t centered) { IOS_TRACE("resize request rejected: iOS owns window size requested=%ux%u", w, h); return FALSE; }
bool_t axCreateSurface(uint32_t w, uint32_t h) { IOS_TRACE("offscreen window unsupported size=%ux%u", w, h); return FALSE; }
bool_t axSetSwapInterval(int interval) {
  if (interval < 0 || interval > 1) { IOS_TRACE("swap interval rejected=%d", interval); return FALSE; }
  return TRUE;
}
