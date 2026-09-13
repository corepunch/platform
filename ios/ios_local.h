#ifndef __IOS_LOCAL_H__
#define __IOS_LOCAL_H__
#import <UIKit/UIKit.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/ES3/gl.h>
#include "../platform.h"
#define IOS_TRACE(...) do { fprintf(stderr, "[ios] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)
@interface AXView : UIView <UIKeyInput>
@property(nonatomic, strong) UITouch *active_touch;
- (void)cancel_touch;
@end
extern UIWindow *ios_window;
extern AXView *ios_view;
extern EAGLContext *ios_context;
extern CADisplayLink *ios_display_link;
void ios_resize_surface(void);
void ios_cancel_timers(void);
#endif
