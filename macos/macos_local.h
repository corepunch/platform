#ifndef __MACOS_LOCAL_H__
#define __MACOS_LOCAL_H__

#import <AppKit/AppKit.h>

#include "../platform.h"

@interface WindowDelegate : NSObject<NSWindowDelegate>
@property (assign) NSWindow* window;
@end

//@interface StatusToolBarItem: NSView
//@end

/* The most recent left mouse-down, kept for axDragWindow. */
extern NSEvent *macos_last_mouse_down;

struct tracking_area *FindTrackingArea(float x, float y);

#endif
