#ifndef __IOS_LOCAL_H__
#define __IOS_LOCAL_H__
#import <UIKit/UIKit.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/ES3/gl.h>
#include "../platform.h"
#define IOS_TRACE(...) do { fprintf(stderr, "[ios] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)
@interface AXView : UIView <UIKeyInput>
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
extern EAGLContext *ios_context;
extern CADisplayLink *ios_display_link;
void ios_resize_surface(void);
void ios_cancel_timers(void);
void ios_post_gesture(ax_gesture_t gesture);
void ios_post_touch(uint32_t event, uint32_t wparam, void *lparam, ax_pointer_t pointer);
#endif
