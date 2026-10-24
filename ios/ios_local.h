#ifndef __IOS_LOCAL_H__
#define __IOS_LOCAL_H__

#include "../platform.h"

#ifdef __OBJC__
#import <UIKit/UIApplication.h>
#import <UIKit/UIWindow.h>
#import <UIKit/UIViewController.h>
#import <UIKit/UITextInput.h>
#import <UIKit/UITextInputTraits.h>
UIWindow *axIOSGetWindow(void);
#endif

void axIOSShowKeyboard(bool_t visible);
void axIOSShutdownEvents(void);

#endif
