#ifndef __IOS_LOCAL_H__
#define __IOS_LOCAL_H__
#import <UIKit/UIKit.h>
#if AX_METAL
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#else
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/ES3/gl.h>
#endif
#include "../platform.h"
#include "ios_message_queue.h"
#if __has_feature(objc_arc)
#define AXIOS_RELEASE(object) ((void)0)
#define AXIOS_AUTORELEASE(object) (object)
#else
#define AXIOS_RELEASE(object) [(object) release]
#define AXIOS_AUTORELEASE(object) [(object) autorelease]
#endif
#define IOS_TRACE(...) do { fprintf(stderr, "[ios] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)
@interface AXView : UIView <UIKeyInput>
@property(nonatomic) UIKeyboardType keyboardType;
@property(nonatomic, getter=isSecureTextEntry) BOOL secureTextEntry;
@property(nonatomic, strong) UITouch *active_touch;
@property(nonatomic, strong) UITouch *gesture_first;
@property(nonatomic, strong) UITouch *gesture_second;
@property(nonatomic) CGPoint gesture_center;
@property(nonatomic) CGPoint gesture_vector;
- (void)cancel_touch;
- (void)end_gesture:(BOOL)cancelled;
@end
extern UIWindow *ios_window;
extern AXView *ios_view;
#if !AX_METAL
extern EAGLContext *ios_context;
#endif
bool_t ios_has_surface(void);
extern CADisplayLink *ios_display_link;
void ios_resize_surface(void);
void ios_cancel_timers(void);
void ios_install_drop(UIView *view);
void ios_post_gesture(ax_gesture_t gesture);
void ios_post_touch(uint32_t event, uint32_t wparam, void *lparam, ax_pointer_t pointer);
UIWindow *axIOSGetWindow(void);
void axIOSShowKeyboard(bool_t visible, UIKeyboardType keyboard, bool_t secure);
void axIOSShutdownEvents(void);
bool_t ios_external_lifecycle(void);
#endif
