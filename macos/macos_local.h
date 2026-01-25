#ifndef __MACOS_LOCAL_H__
#define __MACOS_LOCAL_H__

#import <AppKit/AppKit.h>

#include <include/api.h>
#include <include/event.h>
#include <include/renderer.h>

@interface WindowDelegate : NSObject<NSWindowDelegate>
@property (weak) NSWindow* window;
@end

//@interface StatusToolBarItem: NSView
//@end

struct tracking_area *FindTrackingArea(float x, float y);

#endif
