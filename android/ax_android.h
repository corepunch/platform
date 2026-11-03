#ifndef __PLATFORM_ANDROID_H__
#define __PLATFORM_ANDROID_H__

/* Android-only API. The application's android_main() hands the glue state to
 * the platform before axInit(); everything else follows platform.h. */
#include "../platform.h"

struct android_app;

/* Required before axInit(). The platform installs its command and input
 * handlers on the app and drives ALooper from axWaitMessage/axPeekMessage. */
AX_API void axAndroidSetApp(struct android_app *app);
AX_API struct android_app *axAndroidGetApp(void);

/* Thread-safe post for Java/UI-thread callbacks (text input, links, insets).
 * Copies the message; wakes a blocked axWaitMessage. */
AX_API void axAndroidPostMessage(struct AXmessage message);

/* Window insets in points: system bars and the soft keyboard above them. */
AX_API void axAndroidSetInsets(float left, float top, float right, float bottom, float keyboard);
AX_API void axAndroidGetSafeArea(float *left, float *top, float *right, float *bottom);
AX_API float axAndroidGetKeyboardInset(void);

/* True between APP_CMD_INIT_WINDOW and APP_CMD_TERM_WINDOW. */
AX_API bool_t axAndroidHasWindow(void);
/* Set when the activity is finishing; the application should return. */
AX_API bool_t axAndroidDestroyRequested(void);

#endif
