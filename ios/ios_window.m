#include "ios_local.h"
#include <unistd.h>
#include <math.h>

@interface AXView ()
@property(nonatomic) CGPoint touch_origin;
@property(nonatomic) CGPoint touch_previous;
@property(nonatomic) CGPoint scroll_remainder;
@property(nonatomic) int touch_mode;
@end

UIWindow *ios_window;
AXView *ios_view;
EAGLContext *ios_context;
CADisplayLink *ios_display_link;
static GLuint ios_framebuffer, ios_color, ios_depth;
static int ios_width, ios_height, ios_pixel_w, ios_pixel_h;
static float ios_scale = 1, ios_keyboard_inset;
static bool_t (*ios_start)(int, char **);
static void (*ios_frame)(void), (*ios_stop)(void);
static int ios_argc;
static char **ios_argv;
static bool_t ios_started, ios_in_frame, ios_external_host;

static ax_pointer_t ios_pointer(UITouch *touch) {
  ax_pointer_t p = {0};
  if (touch.type == UITouchTypePencil) {
    p.flags = AX_POINTER_STYLUS;
    p.altitude = (float)touch.altitudeAngle;
  } else if (touch.type == UITouchTypeDirect) {
    p.flags = AX_POINTER_TOUCH;
  }
  p.time = (uint32_t)(uint64_t)(touch.timestamp * 1000);
  return p;
}

static uint32_t ios_point(CGPoint p) {
  return MAKEDWORD((int)CLAMP(p.x, 0, UINT16_MAX), (int)CLAMP(p.y, 0, UINT16_MAX));
}

static void ios_scroll(UITouch *touch, CGPoint origin, uint32_t phase, int dx, int dy) {
  ax_pointer_t pointer = ios_pointer(touch);
  pointer.flags |= phase;
  ios_post_touch(kEventScrollWheel, ios_point(origin),
                 (void *)(uintptr_t)MAKEDWORD(dx, dy), pointer);
}

static void ios_touch(UITouch *touch, uint32_t event) {
  CGPoint p = [touch locationInView:ios_view];
  CGPoint old = [touch previousLocationInView:ios_view];
  if (event != kEventLeftButtonDragged)
    IOS_TRACE("touch view=%p event=%u x=%.1f y=%.1f pencil=%d altitude=%.2f",
              (__bridge void *)ios_view, event, p.x, p.y,
              touch.type == UITouchTypePencil, (double)touch.altitudeAngle);
  ios_post_touch(event, ios_point(p),
    event == kEventLeftButtonDragged ? (void *)(intptr_t)MAKEDWORD((int)(p.x - old.x), (int)(p.y - old.y)) : NULL,
    ios_pointer(touch));
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
- (UITextAutocorrectionType)autocorrectionType { return UITextAutocorrectionTypeNo; }
- (UITextSpellCheckingType)spellCheckingType { return UITextSpellCheckingTypeNo; }
- (UITextAutocapitalizationType)autocapitalizationType { return UITextAutocapitalizationTypeNone; }
- (UITextSmartQuotesType)smartQuotesType { return UITextSmartQuotesTypeNo; }
- (UITextSmartDashesType)smartDashesType { return UITextSmartDashesTypeNo; }
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) if (!ios_key(press, kEventKeyDown, TRUE)) [super pressesBegan:[NSSet setWithObject:press] withEvent:event];
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) if (!ios_key(press, kEventKeyUp, TRUE)) [super pressesEnded:[NSSet setWithObject:press] withEvent:event];
}
- (void)insertText:(NSString *)text {
  IOS_TRACE("text input length=%lu", (unsigned long)text.length);
  const char *utf8 = text.UTF8String;
  if (!utf8) return;
  for (size_t i = 0, length = strlen(utf8); i < length;) {
    unsigned char c = utf8[i];
    size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    n = MIN(n, length - i);
    void *chars = NULL;
    memcpy(&chars, utf8 + i, n);
    BOOL enter = c == '\n' || c == '\r';
    if (ios_external_host && !enter) axPostMessageW(NULL, kEventChar, 0, chars);
    else {
      uint32_t key = n == 1 ? c : c & ((1u << (7 - n)) - 1);
      for (size_t j = 1; j < n; j++) key = (key << 6) | (utf8[i + j] & 0x3f);
      if (enter) key = AX_KEY_ENTER;
      axPostMessageW(NULL, kEventKeyDown, key, enter ? NULL : chars);
      axPostMessageW(NULL, kEventKeyUp, key, NULL);
    }
    i += n;
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
      AXIOS_RELEASE(fingers);
      return;
    }
    AXIOS_RELEASE(fingers);
  }
  UITouch *touch = touches.anyObject;
  for (UITouch *candidate in touches) if (candidate.type == UITouchTypePencil) touch = candidate;
  if (self.active_touch) {
    if (touch.type != UITouchTypePencil || self.active_touch.type == UITouchTypePencil) return;
    [self cancel_touch];
  }
  self.active_touch = touch;
  self.touch_origin = self.touch_previous = [touch locationInView:self];
  self.scroll_remainder = CGPointZero;
  self.touch_mode = ios_external_host && touch.type == UITouchTypeDirect ? 0 : 1;
  if (!self.touch_mode) {
    ios_scroll(touch, self.touch_origin, AX_POINTER_SCROLL_BEGIN, 0, 0);
    ios_touch(touch, kEventMouseMoved);
    return;
  }
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
  if (ios_external_host && self.active_touch.type == UITouchTypeDirect) {
    CGPoint p = [self.active_touch locationInView:self];
    if (!self.touch_mode) {
      CGFloat dx = p.x - self.touch_origin.x, dy = p.y - self.touch_origin.y;
      if (hypot(dx, dy) < 8) return;
      // Determine direction before a press so scrolling cannot activate a button.
      self.touch_mode = fabs(dy) >= fabs(dx) ? 2 : 1;
      if (self.touch_mode == 1) {
        ios_scroll(self.active_touch, self.touch_origin, AX_POINTER_SCROLL_CANCEL, 0, 0);
        ios_post_touch(kEventLeftButtonDown, ios_point(self.touch_origin), NULL, ios_pointer(self.active_touch));
      }
    }
    if (self.touch_mode == 2) {
      CGPoint remainder = self.scroll_remainder;
      remainder.x += p.x - self.touch_previous.x;
      remainder.y += p.y - self.touch_previous.y;
      int dx = (int)lround(remainder.x), dy = (int)lround(remainder.y);
      self.scroll_remainder = CGPointMake(remainder.x - dx, remainder.y - dy);
      self.touch_previous = p;
      if (dx || dy)
        ios_scroll(self.active_touch, self.touch_origin, AX_POINTER_SCROLL_UPDATE, dx, dy);
      return;
    }
  }
  for (UITouch *touch in [event coalescedTouchesForTouch:self.active_touch] ?: @[self.active_touch])
    ios_touch(touch, kEventLeftButtonDragged);
}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if ([touches containsObject:self.gesture_first] || [touches containsObject:self.gesture_second]) [self end_gesture:NO];
  if ([touches containsObject:self.active_touch]) {
    if (ios_external_host && self.active_touch.type == UITouchTypeDirect)
      ios_scroll(self.active_touch, self.touch_origin, AX_POINTER_SCROLL_END, 0, 0);
    if (!self.touch_mode) {
      ios_touch(self.active_touch, kEventLeftButtonDown);
      if (self.active_touch.tapCount == 2) ios_touch(self.active_touch, kEventLeftDoubleClick);
    }
    if (self.touch_mode != 2) ios_touch(self.active_touch, kEventLeftButtonUp);
    self.active_touch = nil;
  }
}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
  if ([touches containsObject:self.gesture_first] || [touches containsObject:self.gesture_second]) [self end_gesture:YES];
  if ([touches containsObject:self.active_touch]) [self cancel_touch];
}
- (void)cancel_touch {
  if (self.active_touch) {
    if (ios_external_host && self.active_touch.type == UITouchTypeDirect)
      ios_scroll(self.active_touch, self.touch_origin, AX_POINTER_SCROLL_CANCEL, 0, 0);
    if (!ios_external_host) ios_touch(self.active_touch, kEventPointerCancel);
    else if (self.touch_mode == 1)
      ios_post_touch(kEventLeftButtonUp, MAKEDWORD(UINT16_MAX, UINT16_MAX), NULL, ios_pointer(self.active_touch));
  }
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
- (void)safeAreaInsetsDidChange {
  [super safeAreaInsetsDidChange];
  if (ios_external_host)
    axPostMessageW(NULL, kEventWindowResized, axGetSize(NULL), NULL);
}
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
#if !__has_feature(objc_arc)
- (void)dealloc {
  [_active_touch release];
  [_gesture_first release];
  [_gesture_second release];
  [super dealloc];
}
#endif
@end

@interface AXController : UIViewController
@property(nonatomic) CGRect keyboard_frame;
@property(nonatomic) UIStatusBarStyle status_bar_style;
@end
@implementation AXController
- (BOOL)canBecomeFirstResponder { return YES; }
- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) ios_key(press, kEventKeyDown, FALSE);
}
- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
  for (UIPress *press in presses) ios_key(press, kEventKeyUp, FALSE);
}
- (BOOL)prefersStatusBarHidden { return !ios_external_host; }
- (UIStatusBarStyle)preferredStatusBarStyle { return self.status_bar_style; }
- (BOOL)prefersHomeIndicatorAutoHidden { return !ios_external_host; }
- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.systemBackgroundColor;
  ios_view = [[AXView alloc] initWithFrame:self.view.bounds];
  ios_view.multipleTouchEnabled = YES;
  [self.view addSubview:ios_view];
  if (!ios_external_host) {
    ios_view.translatesAutoresizingMaskIntoConstraints = NO;
    // Standalone platform clients render beneath the system screen insets.
    [NSLayoutConstraint activateConstraints:@[
      [ios_view.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
      [ios_view.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
      [ios_view.topAnchor constraintEqualToAnchor:self.view.topAnchor],
      [ios_view.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]]];
  }
  ios_install_drop(ios_view);
  UIHoverGestureRecognizer *hover = [[UIHoverGestureRecognizer alloc] initWithTarget:ios_view action:@selector(hover:)];
  [ios_view addGestureRecognizer:hover];
  AXIOS_RELEASE(hover);
  UIPanGestureRecognizer *scroll = [[UIPanGestureRecognizer alloc] initWithTarget:ios_view action:@selector(scroll:)];
  scroll.minimumNumberOfTouches = 2;
  scroll.allowedTouchTypes = @[@(UITouchTypeIndirectPointer)];
  scroll.allowedScrollTypesMask = UIScrollTypeMaskAll;
  [ios_view addGestureRecognizer:scroll];
  AXIOS_RELEASE(scroll);
  for (NSNotificationName name in @[UIKeyboardWillChangeFrameNotification, UIKeyboardWillHideNotification])
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(keyboard:) name:name object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(application_inactive:)
                                           name:UIApplicationWillResignActiveNotification object:nil];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(application_active:)
                                           name:UIApplicationDidBecomeActiveNotification object:nil];
}
- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  if (!ios_external_host) return;
  CGRect frame = self.view.bounds;
  float inset = 0;
  if (!CGRectIsEmpty(self.keyboard_frame)) {
    id<UICoordinateSpace> screen = self.view.window.screen.coordinateSpace;
    CGRect keyboard = screen ? [self.view convertRect:self.keyboard_frame fromCoordinateSpace:screen] : CGRectZero;
    CGRect hidden = CGRectIntersection(frame, keyboard);
    if (!CGRectIsEmpty(hidden) && CGRectGetMaxY(keyboard) >= CGRectGetMaxY(frame) &&
        CGRectGetMinX(keyboard) <= CGRectGetMinX(frame) &&
        CGRectGetMaxX(keyboard) >= CGRectGetMaxX(frame))
      inset = hidden.size.height;
  }
  ios_view.frame = frame;
  [ios_view setNeedsLayout];
  [ios_view layoutIfNeeded];
  if (ios_keyboard_inset != inset) {
    ios_keyboard_inset = inset;
    axPostMessageW(NULL, kEventWindowResized, axGetSize(NULL), NULL);
    axPostMessageW(NULL, kEventWindowPaint, axGetSize(NULL), NULL);
  }
}
// The app slides up by the part of the window the on-screen keyboard covers.
// A view transform keeps the drawable size, and locationInView: still reports
// touches in the view's own coordinates.
- (void)keyboard:(NSNotification *)note {
  if (ios_external_host) {
    self.keyboard_frame = [note.name isEqualToString:UIKeyboardWillHideNotification]
                       ? CGRectZero : [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
    return;
  }
  CGFloat cover = 0;
  if (![note.name isEqualToString:UIKeyboardWillHideNotification]) {
    CGRect end = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    id<UICoordinateSpace> screen = self.view.window.screen.coordinateSpace;
    CGRect kb = screen ? [self.view convertRect:end fromCoordinateSpace:screen] : CGRectZero;
    CGRect hidden = CGRectIntersection(kb, self.view.bounds);
    if (!CGRectIsNull(hidden) && CGRectGetMaxY(kb) >= CGRectGetMaxY(self.view.bounds)) cover = hidden.size.height;
  }
  IOS_TRACE("keyboard cover=%.1f", cover);
  double duration = [note.userInfo[UIKeyboardAnimationDurationUserInfoKey] doubleValue];
  UIViewAnimationOptions curve = (UIViewAnimationOptions)[note.userInfo[UIKeyboardAnimationCurveUserInfoKey] integerValue] << 16;
  [UIView animateWithDuration:duration delay:0 options:curve | UIViewAnimationOptionBeginFromCurrentState
                   animations:^{ ios_view.transform = CGAffineTransformMakeTranslation(0, -cover); } completion:nil];
}
- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self becomeFirstResponder];
  if (!ios_start || ios_started) return;
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
- (void)application_inactive:(NSNotification *)notification {
  (void)notification;
  if (!ios_external_host) return;
  [ios_view cancel_touch];
  [ios_view end_gesture:YES];
  axPostMessageW(NULL, kEventKillFocus, 0, NULL);
}
- (void)application_active:(NSNotification *)notification {
  (void)notification;
  if (!ios_external_host) return;
  axPostMessageW(NULL, kEventSetFocus, 0, NULL);
  axPostMessageW(NULL, kEventWindowPaint, axGetSize(NULL), NULL);
}
- (void)dealloc {
  [NSNotificationCenter.defaultCenter removeObserver:self];
#if !__has_feature(objc_arc)
  [super dealloc];
#endif
}
@end

@interface AXSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation AXSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  self.window = AXIOS_AUTORELEASE([[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene]);
  ios_window = self.window;
  AXController *host = [AXController new];
  self.window.rootViewController = host;
  AXIOS_RELEASE(host);
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
#if !__has_feature(objc_arc)
- (void)dealloc { [_window release]; [super dealloc]; }
#endif
@end

@interface AXApplicationDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation AXApplicationDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
  UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Orion" sessionRole:session.role];
  config.delegateClass = AXSceneDelegate.class;
  return AXIOS_AUTORELEASE(config);
}
- (void)applicationWillTerminate:(UIApplication *)application { if (ios_started && ios_stop) ios_stop(); }
@end

int axRunApplication(int argc, char **argv, bool_t (*start)(int, char **), void (*frame)(void), void (*stop)(void)) {
  if (!start || !frame) { IOS_TRACE("application callbacks missing"); return 1; }
  ios_external_host = FALSE;
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
  if (![NSThread isMainThread]) return FALSE;
  if (ios_context) { axMakeCurrentContext(); return TRUE; }
  if (!ios_view) {
    UIWindowScene *window_scene = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
      if (![scene isKindOfClass:UIWindowScene.class]) continue;
      window_scene = (UIWindowScene *)scene;
      if (scene.activationState == UISceneActivationStateForegroundActive ||
          scene.activationState == UISceneActivationStateForegroundInactive) break;
    }
    if (!window_scene) { IOS_TRACE("create window requires a connected UIWindowScene"); return FALSE; }
    // ORCA already owns UIApplicationMain and its per-scene display link.
    ios_external_host = TRUE;
    ios_window = [[UIWindow alloc] initWithWindowScene:window_scene];
    ios_window.frame = window_scene.coordinateSpace.bounds;
    AXController *host = [AXController new];
    host.title = title ? [NSString stringWithUTF8String:title] : @"ORCA";
    ios_window.rootViewController = host;
    [host loadViewIfNeeded];
    if (!(flags & AX_WINDOW_HIDDEN)) [ios_window makeKeyAndVisible];
    [host.view setNeedsLayout];
    [host.view layoutIfNeeded];
    AXIOS_RELEASE(host);
  }
  ios_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
  if (!ios_context) {
    IOS_TRACE("OpenGL ES 3 context creation failed");
    axShutdown();
    return FALSE;
  }
  [EAGLContext setCurrentContext:ios_context];
  ios_scale = ios_external_host || (flags & AX_WINDOW_HIGHDPI) ? ios_window.screen.scale : 1;
  ios_view.contentScaleFactor = ios_scale;
  CAEAGLLayer *layer = (CAEAGLLayer *)ios_view.layer;
  layer.opaque = YES;
  layer.drawableProperties = @{kEAGLDrawablePropertyRetainedBacking: @YES, kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8};
  glGenFramebuffers(1, &ios_framebuffer);
  glGenRenderbuffers(1, &ios_color);
  glGenRenderbuffers(1, &ios_depth);
  ios_resize_surface();
  IOS_TRACE("create window title=%s requested=%ux%u", title, w, h);
  if (ios_width > 0 && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) return TRUE;
  axShutdown();
  return FALSE;
}
void axInit(void) {
  if (!ios_start) return;
  NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
  if (chdir(documents.fileSystemRepresentation)) IOS_TRACE("could not select Documents working directory");
}
void axShutdown(void) {
  [ios_display_link invalidate]; ios_display_link = nil;
  [ios_view cancel_touch];
  [ios_view end_gesture:YES];
  axIOSShutdownEvents();
  [EAGLContext setCurrentContext:ios_context];
  glDeleteFramebuffers(1, &ios_framebuffer); glDeleteRenderbuffers(1, &ios_color); glDeleteRenderbuffers(1, &ios_depth);
  ios_framebuffer = ios_color = ios_depth = 0;
  [EAGLContext setCurrentContext:nil];
  AXIOS_RELEASE(ios_context);
  ios_context = nil;
  ios_width = ios_height = ios_pixel_w = ios_pixel_h = 0;
  if (ios_external_host) {
    [ios_view resignFirstResponder];
    ios_window.hidden = YES;
    ios_window.rootViewController = nil;
    AXIOS_RELEASE(ios_view);
    ios_view = nil;
    AXIOS_RELEASE(ios_window);
    ios_window = nil;
    ios_external_host = FALSE;
  }
  ios_keyboard_inset = 0;
}

UIWindow *axIOSGetWindow(void) { return ios_window; }
bool_t ios_external_lifecycle(void) { return ios_start == NULL; }
void axIOSShowKeyboard(bool_t visible, UIKeyboardType keyboard, bool_t secure) {
  if (!ios_view) return;
  if (visible) {
    BOOL changed = ios_view.keyboardType != keyboard || ios_view.secureTextEntry != !!secure;
    ios_view.keyboardType = keyboard;
    ios_view.secureTextEntry = !!secure;
    if (changed && ios_view.isFirstResponder) [ios_view reloadInputViews];
    if (!ios_view.isFirstResponder && !ios_window.rootViewController.presentedViewController &&
        UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
      axSetTextInput(TRUE);
  } else if (ios_view.isFirstResponder) axSetTextInput(FALSE);
}
float axGetScaling(void) { return ios_scale; }
float axIOSGetKeyboardInset(void) { return ios_keyboard_inset; }
void axIOSGetSafeArea(float *left, float *top, float *right, float *bottom) {
  UIEdgeInsets insets = ios_view.safeAreaInsets;
  if (left) *left = insets.left;
  if (top) *top = insets.top;
  if (right) *right = insets.right;
  if (bottom) *bottom = insets.bottom;
}
void axIOSSetBackgroundColor(float red, float green, float blue) {
  if (!ios_external_host || !ios_window) return;
  AXController *host = (AXController *)ios_window.rootViewController;
  UIColor *color = [UIColor colorWithRed:red green:green blue:blue alpha:1];
  if (![host.view.backgroundColor isEqual:color]) host.view.backgroundColor = color;
  UIStatusBarStyle style = .2126 * red + .7152 * green + .0722 * blue > .5
                        ? UIStatusBarStyleDarkContent : UIStatusBarStyleLightContent;
  if (host.status_bar_style != style) {
    host.status_bar_style = style;
    [host setNeedsStatusBarAppearanceUpdate];
  }
}
uint32_t axGetSize(struct AXsize *size) {
  if (size) { size->width = ios_width; size->height = ios_height; }
  return MAKEDWORD(ios_width, ios_height);
}
void axMakeCurrentContext(void) { [EAGLContext setCurrentContext:ios_context]; }
void axBindFramebuffer(void) { glBindFramebuffer(GL_FRAMEBUFFER, ios_framebuffer); }
void axBeginPaint(void) { axMakeCurrentContext(); axBindFramebuffer(); }
void axEndPaint(void) {
  if (!ios_context || UIApplication.sharedApplication.applicationState == UIApplicationStateBackground) return;
  glBindRenderbuffer(GL_RENDERBUFFER, ios_color);
  if (![ios_context presentRenderbuffer:GL_RENDERBUFFER]) IOS_TRACE("present failed");
}
bool_t axSetSize(uint32_t w, uint32_t h, bool_t centered) {
  if (ios_external_host && w == (uint32_t)ios_width && h == (uint32_t)ios_height) return TRUE;
  IOS_TRACE("resize request rejected: iOS owns window size requested=%ux%u centered=%d", w, h, centered);
  return FALSE;
}
bool_t axCreateSurface(uint32_t w, uint32_t h) { IOS_TRACE("offscreen window unsupported size=%ux%u", w, h); return FALSE; }
bool_t axSetSwapInterval(int interval) {
  if (interval < 0 || interval > 1) { IOS_TRACE("swap interval rejected=%d", interval); return FALSE; }
  return TRUE;
}
