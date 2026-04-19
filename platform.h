/**
 * @file platform.h
 * @brief Cross-platform window, input, and rendering abstraction layer.
 *
 * This header defines the public API for the platform library, providing
 * a unified interface for window management, event handling, OpenGL/EGL
 * rendering context management, file dialogs, and system utilities across
 * macOS, Linux (Wayland or X11), QNX, and WebGL (Emscripten) targets.
 *
 * All public symbols are exported via the #AX_API visibility macro.
 */

#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include "events.h"

#ifndef LOWORD
/** @brief Extract the low 16 bits of a 32-bit value. */
#define LOWORD(l) ((uint16_t)(l & 0xFFFF))
#endif

#ifndef HIWORD
/** @brief Extract the high 16 bits of a 32-bit value. */
#define HIWORD(l) ((uint16_t)((l >> 16) & 0xFFFF))
#endif

#ifndef MAKEDWORD
/** @brief Combine two 16-bit halves into a 32-bit value. */
#define MAKEDWORD(low, high) ((uint32_t)(((uint16_t)(low)) | ((uint32_t)((uint16_t)(high))) << 16))
#endif

#ifndef MAX
/** @brief Return the larger of two values. */
#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#endif

#ifndef MIN
/** @brief Return the smaller of two values. */
#define MIN(x, y) ((x) <= (y) ? (x) : (y))
#endif

#ifndef CLAMP
/** @brief Clamp @p x to the closed interval [@p min, @p max]. */
#define CLAMP(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif

/**
 * @brief Marks a function as part of the public platform API.
 *
 * On GCC/Clang this expands to `__attribute__((visibility("default")))`,
 * ensuring the symbol is exported from a shared library.  On Windows/MinGW
 * it expands to `__declspec(dllexport)` when building the library
 * (`PLATFORM_BUILD` defined) or `__declspec(dllimport)` when consuming it.
 * Define this macro before including the header to override the default.
 */
#ifndef AX_API
#  if defined(_WIN32) || defined(__MINGW32__)
#    ifdef PLATFORM_BUILD
#      define AX_API __declspec(dllexport)
#    else
#      define AX_API __declspec(dllimport)
#    endif
#  else
#    define AX_API __attribute__((visibility("default")))
#  endif
#endif

/** @defgroup types Basic types
 *  @{
 */
#ifndef TRUE
#define TRUE  1 /**< Boolean true value. */
#endif
#ifndef FALSE
#define FALSE 0 /**< Boolean false value. */
#endif
typedef unsigned int   bool_t;      /**< Boolean: `TRUE` (non-zero) or `FALSE` (0). */
typedef unsigned long  longTime_t;  /**< Millisecond timestamp / duration. */
typedef uint32_t       wParam_t;    /**< Word-sized event parameter. */
typedef void*          lParam_t;    /**< Pointer-sized event parameter. */
typedef unsigned char  byte_t;      /**< Single unsigned byte. */
/** @} */

/**
 * @defgroup keycodes Key codes
 * @brief Virtual key-code constants used in #AX_Message::wParam for key events.
 *
 * ASCII printable characters (32–126) map directly to their ASCII values.
 * Special keys start at 128 to avoid ambiguity.
 * @{
 */
enum
{
  AX_KEY_TAB        = 9,   /**< Tab key. */
  AX_KEY_ENTER      = 13,  /**< Return / Enter key. */
  AX_KEY_ESCAPE     = 27,  /**< Escape key. */
  AX_KEY_SPACE      = 32,  /**< Space bar. */
  AX_KEY_EXCLAIM    = 33,  /**< ! key. */
  AX_KEY_QUOTEDBL   = 34,  /**< " key. */
  AX_KEY_HASH       = 35,  /**< # key. */
  AX_KEY_DOLLAR     = 36,  /**< $ key. */
  AX_KEY_PERCENT    = 37,  /**< % key. */
  AX_KEY_AMPERSAND  = 38,  /**< & key. */
  AX_KEY_QUOTE      = 39,  /**< ' key. */
  AX_KEY_LEFTPAREN  = 40,  /**< ( key. */
  AX_KEY_RIGHTPAREN = 41,  /**< ) key. */
  AX_KEY_ASTERISK   = 42,  /**< * key. */
  AX_KEY_PLUS       = 43,  /**< + key. */
  AX_KEY_COMMA      = 44,  /**< , key. */
  AX_KEY_MINUS      = 45,  /**< - key. */
  AX_KEY_PERIOD     = 46,  /**< . key. */
  AX_KEY_SLASH      = 47,  /**< / key. */
  AX_KEY_0          = 48,  /**< 0 key. */
  AX_KEY_1          = 49,  /**< 1 key. */
  AX_KEY_2          = 50,  /**< 2 key. */
  AX_KEY_3          = 51,  /**< 3 key. */
  AX_KEY_4          = 52,  /**< 4 key. */
  AX_KEY_5          = 53,  /**< 5 key. */
  AX_KEY_6          = 54,  /**< 6 key. */
  AX_KEY_7          = 55,  /**< 7 key. */
  AX_KEY_8          = 56,  /**< 8 key. */
  AX_KEY_9          = 57,  /**< 9 key. */
  AX_KEY_COLON      = 58,  /**< : key. */
  AX_KEY_SEMICOLON  = 59,  /**< ; key. */
  AX_KEY_LESS       = 60,  /**< < key. */
  AX_KEY_EQUALS     = 61,  /**< = key. */
  AX_KEY_GREATER    = 62,  /**< > key. */
  AX_KEY_QUESTION   = 63,  /**< ? key. */
  AX_KEY_AT         = 64,  /**< @ key. */
  AX_KEY_A          = 65,  /**< A key. */
  AX_KEY_B          = 66,  /**< B key. */
  AX_KEY_C          = 67,  /**< C key. */
  AX_KEY_D          = 68,  /**< D key. */
  AX_KEY_E          = 69,  /**< E key. */
  AX_KEY_F          = 70,  /**< F key. */
  AX_KEY_G          = 71,  /**< G key. */
  AX_KEY_H          = 72,  /**< H key. */
  AX_KEY_I          = 73,  /**< I key. */
  AX_KEY_J          = 74,  /**< J key. */
  AX_KEY_K          = 75,  /**< K key. */
  AX_KEY_L          = 76,  /**< L key. */
  AX_KEY_M          = 77,  /**< M key. */
  AX_KEY_N          = 78,  /**< N key. */
  AX_KEY_O          = 79,  /**< O key. */
  AX_KEY_P          = 80,  /**< P key. */
  AX_KEY_Q          = 81,  /**< Q key. */
  AX_KEY_R          = 82,  /**< R key. */
  AX_KEY_S          = 83,  /**< S key. */
  AX_KEY_T          = 84,  /**< T key. */
  AX_KEY_U          = 85,  /**< U key. */
  AX_KEY_V          = 86,  /**< V key. */
  AX_KEY_W          = 87,  /**< W key. */
  AX_KEY_X          = 88,  /**< X key. */
  AX_KEY_Y          = 89,  /**< Y key. */
  AX_KEY_Z          = 90,  /**< Z key. */
  AX_KEY_LEFTBRACKET = 91, /**< [ key. */
  AX_KEY_BACKSLASH   = 92, /**< \ key. */
  AX_KEY_RIGHTBRACKET = 93, /**< ] key. */
  AX_KEY_CARET      = 94,  /**< ^ key. */
  AX_KEY_UNDERSCORE = 95,  /**< _ key. */
  AX_KEY_GRAVE      = 96,  /**< ` key. */
  AX_KEY_LEFTBRACE  = 123, /**< { key. */
  AX_KEY_PIPE       = 124, /**< | key. */
  AX_KEY_RIGHTBRACE = 125, /**< } key. */
  AX_KEY_TILDE      = 126, /**< ~ key. */
  AX_KEY_BACKSPACE  = 127, /**< Backspace / Delete key. */
  AX_KEY_UPARROW    = 128, /**< Up arrow. */
  AX_KEY_DOWNARROW  = 129, /**< Down arrow. */
  AX_KEY_LEFTARROW  = 130, /**< Left arrow. */
  AX_KEY_RIGHTARROW = 131, /**< Right arrow. */
  AX_KEY_ALT        = 132, /**< Alt / Option key. */
  AX_KEY_CTRL       = 133, /**< Control key. */
  AX_KEY_SHIFT      = 134, /**< Shift key. */
  AX_KEY_F1         = 135, /**< F1 function key. */
  AX_KEY_F2         = 136, /**< F2 function key. */
  AX_KEY_F3         = 137, /**< F3 function key. */
  AX_KEY_F4         = 138, /**< F4 function key. */
  AX_KEY_F5         = 139, /**< F5 function key. */
  AX_KEY_F6         = 140, /**< F6 function key. */
  AX_KEY_F7         = 141, /**< F7 function key. */
  AX_KEY_F8         = 142, /**< F8 function key. */
  AX_KEY_F9         = 143, /**< F9 function key. */
  AX_KEY_F10        = 144, /**< F10 function key. */
  AX_KEY_F11        = 145, /**< F11 function key. */
  AX_KEY_F12        = 146, /**< F12 function key. */
  AX_KEY_INS        = 147, /**< Insert key. */
  AX_KEY_DEL        = 148, /**< Delete key. */
  AX_KEY_PGDN       = 149, /**< Page Down key. */
  AX_KEY_PGUP       = 150, /**< Page Up key. */
  AX_KEY_HOME       = 151, /**< Home key. */
  AX_KEY_END        = 152, /**< End key. */
  AX_KEY_KP_HOME        = 160, /**< Keypad Home (7). */
  AX_KEY_KP_UPARROW     = 161, /**< Keypad Up (8). */
  AX_KEY_KP_PGUP        = 162, /**< Keypad Page Up (9). */
  AX_KEY_KP_LEFTARROW   = 163, /**< Keypad Left (4). */
  AX_KEY_KP_5           = 164, /**< Keypad 5. */
  AX_KEY_KP_RIGHTARROW  = 165, /**< Keypad Right (6). */
  AX_KEY_KP_END         = 166, /**< Keypad End (1). */
  AX_KEY_KP_DOWNARROW   = 167, /**< Keypad Down (2). */
  AX_KEY_KP_PGDN        = 168, /**< Keypad Page Down (3). */
  AX_KEY_KP_ENTER       = 169, /**< Keypad Enter. */
  AX_KEY_KP_INS         = 170, /**< Keypad Insert (0). */
  AX_KEY_KP_DEL         = 171, /**< Keypad Delete (.). */
  AX_KEY_KP_SLASH       = 172, /**< Keypad /. */
  AX_KEY_KP_MINUS       = 173, /**< Keypad -. */
  AX_KEY_KP_PLUS        = 174, /**< Keypad +. */
  AX_KEY_PAUSE          = 255, /**< Pause / Break key. */
  AX_KEY_MOUSE1         = 200, /**< Primary mouse button (left). */
  AX_KEY_MOUSE2         = 201, /**< Secondary mouse button (right). */
  AX_KEY_MOUSE3         = 202, /**< Middle mouse button. */
  AX_KEY_JOY1           = 203, /**< Joystick button 1. */
  AX_KEY_JOY2           = 204, /**< Joystick button 2. */
  AX_KEY_JOY3           = 205, /**< Joystick button 3. */
  AX_KEY_JOY4           = 206, /**< Joystick button 4. */
  AX_KEY_AUX1           = 207, /**< Auxiliary button 1. */
  AX_KEY_AUX2           = 208, /**< Auxiliary button 2. */
  AX_KEY_AUX3           = 209, /**< Auxiliary button 3. */
  AX_KEY_AUX4           = 210, /**< Auxiliary button 4. */
  AX_KEY_AUX5           = 211, /**< Auxiliary button 5. */
  AX_KEY_AUX6           = 212, /**< Auxiliary button 6. */
  AX_KEY_AUX7           = 213, /**< Auxiliary button 7. */
  AX_KEY_AUX8           = 214, /**< Auxiliary button 8. */
  AX_KEY_AUX9           = 215, /**< Auxiliary button 9. */
  AX_KEY_AUX10          = 216, /**< Auxiliary button 10. */
  AX_KEY_AUX11          = 217, /**< Auxiliary button 11. */
  AX_KEY_AUX12          = 218, /**< Auxiliary button 12. */
  AX_KEY_AUX13          = 219, /**< Auxiliary button 13. */
  AX_KEY_AUX14          = 220, /**< Auxiliary button 14. */
  AX_KEY_AUX15          = 221, /**< Auxiliary button 15. */
  AX_KEY_AUX16          = 222, /**< Auxiliary button 16. */
  AX_KEY_AUX17          = 223, /**< Auxiliary button 17. */
  AX_KEY_AUX18          = 224, /**< Auxiliary button 18. */
  AX_KEY_AUX19          = 225, /**< Auxiliary button 19. */
  AX_KEY_AUX20          = 226, /**< Auxiliary button 20. */
  AX_KEY_AUX21          = 227, /**< Auxiliary button 21. */
  AX_KEY_AUX22          = 228, /**< Auxiliary button 22. */
  AX_KEY_AUX23          = 229, /**< Auxiliary button 23. */
  AX_KEY_AUX24          = 230, /**< Auxiliary button 24. */
  AX_KEY_AUX25          = 231, /**< Auxiliary button 25. */
  AX_KEY_AUX26          = 232, /**< Auxiliary button 26. */
  AX_KEY_AUX27          = 233, /**< Auxiliary button 27. */
  AX_KEY_AUX28          = 234, /**< Auxiliary button 28. */
  AX_KEY_AUX29          = 235, /**< Auxiliary button 29. */
  AX_KEY_AUX30          = 236, /**< Auxiliary button 30. */
  AX_KEY_AUX31          = 237, /**< Auxiliary button 31. */
  AX_KEY_AUX32          = 238, /**< Auxiliary button 32. */
  AX_KEY_MWHEELDOWN     = 239, /**< Mouse wheel scroll down. */
  AX_KEY_MWHEELUP       = 240, /**< Mouse wheel scroll up. */
};
/** @} */

/**
 * @defgroup modifiers Modifier key flags
 * @brief Bit flags ORed into #AX_Message::wParam for keyboard and mouse events.
 * @{
 */
enum
{
  AX_MOD_SHIFT = 1 << 16, /**< Shift key is held. */
  AX_MOD_CTRL  = 1 << 17, /**< Control key is held. */
  AX_MOD_ALT   = 1 << 18, /**< Alt / Option key is held. */
  AX_MOD_CMD   = 1 << 19, /**< Command / Super / Meta key is held. */
};
/** @} */

/**
 * @brief General-purpose byte buffer.
 */
struct AXbuffer
{
  byte_t* data;      /**< Pointer to the raw byte storage. */
  int     maxsize;   /**< Total capacity in bytes. */
  int     cursize;   /**< Number of bytes currently written. */
  int     readcount; /**< Read cursor position in bytes. */
};

/**
 * @brief Platform-independent event message.
 *
 * Dispatched by #axPollEvent.  The @p message field identifies the event
 * type (one of the `kEvent*` constants from events.h) and determines how the
 * parameter unions should be interpreted.
 *
 * Mouse events:   @p wParam = MAKEDWORD(x, y), @p lParam = scroll/drag delta.
 * Keyboard events: @p wParam = key-code | modifier flags, @p lParam = UTF-8 char.
 * Window events:  @p target = window handle, @p wParam = new width/height.
 */
struct AXmessage
{
  void*    target;   /**< Window or object that should receive the event. */
  uint32_t message;  /**< Event type identifier (e.g. #kEventKeyDown). */
  union {
    wParam_t wParam;                         /**< Generic word parameter. */
    struct { uint16_t x, y; };               /**< Pointer position (mouse events). */
    struct { uint16_t keyCode, modflags; };  /**< Key code and modifier flags (key events). */
  };
  union {
    lParam_t lParam;              /**< Generic pointer parameter. */
    struct { int16_t dx, dy; };   /**< Relative delta (drag / scroll events). */
  };
  uint32_t id; /**< Sequence number assigned at post time. */
};

/**
 * @brief 2-D size in pixels.
 */
struct AXsize
{
  uint32_t width;  /**< Width in pixels. */
  uint32_t height; /**< Height in pixels. */
};

/**
 * @defgroup events Event queue
 * @brief Functions for posting and consuming platform events.
 * @{
 */

/**
 * @brief Post an event to the platform event queue.
 *
 * Thread-safe on all platforms.  Duplicate paint and resize events are
 * coalesced: if an identical message for the same @p hobj is already queued,
 * only its parameters are updated rather than adding a second entry.
 *
 * @param hobj   Target window or object handle (stored in #AX_Message::target).
 * @param event  Event type identifier (e.g. #kEventWindowPaint).
 * @param wparam Word-sized parameter; semantics depend on @p event.
 * @param lparam Pointer-sized parameter; semantics depend on @p event.
 */
AX_API void
axPostMessageW(void* hobj, uint32_t event, uint32_t wparam, void* lparam);

/**
 * @brief Retrieve the next event from the queue without blocking.
 *
 * On macOS this also pumps the native Cocoa run loop to collect pending
 * system events before returning.
 *
 * @param[out] msg  Filled with event data when an event is available.
 * @return 1 if an event was written to @p msg, 0 if the queue is empty.
 */
AX_API int
axPollEvent(struct AXmessage* msg);

/**
 * @brief Remove all queued events whose target matches @p target.
 *
 * Useful when a window is being destroyed to discard stale events before
 * they are delivered.
 *
 * @param target  Window or object handle to match against.
 */
AX_API void
axRemoveFromQueue(void* target);

/**
 * @brief Notify the event system that a file was dropped onto the window.
 *
 * This is called internally by platform-specific drag-and-drop handlers.
 * Applications should not normally call this directly; instead listen for
 * #kEventDragDrop messages via #axPollEvent.
 *
 * @param filename  Null-terminated UTF-8 path of the dropped file.
 * @param x         Horizontal drop position in window coordinates.
 * @param y         Vertical drop position in window coordinates.
 */
AX_API void
axNotifyFileDropEvent(char const *filename, float x, float y);

/** @} */

/**
 * @defgroup dialogs File dialogs
 * @brief Native file-system dialog functions.
 *
 * All dialog functions are synchronous and modal.  On platforms that do not
 * support native file dialogs (Wayland, QNX, WebGL) the functions return
 * `FALSE` immediately without modifying @p lpstrFile.
 * @{
 */

/** @brief Flag: the chosen file must already exist. */
enum {
  OFN_FILEMUSTEXIST = 1 << 0,
  OFN_PATHMUSTEXIST = 1 << 1, /**< The directory portion of the path must exist. */
};

/**
 * @brief Parameters for file and folder dialog functions.
 */
typedef struct _AXopenfilename {
  char       *lpstrFile;   /**< Buffer that receives the chosen path (UTF-8, null-terminated). */
  uint32_t    nMaxFile;    /**< Size of @p lpstrFile in bytes, including the null terminator. */
  char const *lpstrFilter; /**< Optional file-type filter string (platform-specific format). */
  char const *lpstrTitle;  /**< Optional dialog title; NULL uses the platform default. */
  uint32_t    Flags;       /**< Combination of OFN_* flags. */
} AXopenfilename;

/**
 * @brief Show a native open-file dialog.
 *
 * Blocks until the user confirms or cancels.  On success the chosen path is
 * written to @p ofn->lpstrFile (null-terminated, truncated to @p ofn->nMaxFile
 * bytes).
 *
 * @param ofn  Dialog parameters.
 * @return `TRUE` if the user selected a file, `FALSE` if cancelled or unsupported.
 */
AX_API bool_t
axGetOpenFileName(AXopenfilename const *ofn);

/**
 * @brief Show a native save-file dialog.
 *
 * Blocks until the user confirms or cancels.  On success the chosen path is
 * written to @p ofn->lpstrFile (null-terminated, truncated to @p ofn->nMaxFile
 * bytes).
 *
 * @param ofn  Dialog parameters.
 * @return `TRUE` if the user entered a path, `FALSE` if cancelled or unsupported.
 */
AX_API bool_t
axGetSaveFileName(AXopenfilename const *ofn);

/**
 * @brief Show a native folder-picker dialog.
 *
 * Blocks until the user confirms or cancels.  On success the chosen directory
 * path is written to @p ofn->lpstrFile (null-terminated, truncated to
 * @p ofn->nMaxFile bytes).
 *
 * @param ofn  Dialog parameters.
 * @return `TRUE` if the user selected a folder, `FALSE` if cancelled or unsupported.
 */
AX_API bool_t
axGetFolderName(AXopenfilename const *ofn);

/** @} */

/**
 * @defgroup lifecycle Platform lifecycle
 * @brief Initialisation and teardown of the platform subsystem.
 * @{
 */

/**
 * @brief Initialise the platform subsystem.
 *
 * Must be called once before any other AX_* function.  On macOS this starts
 * the NSApplication run loop; on Wayland it connects to the Wayland display
 * and initialises EGL; on QNX it creates the Screen context and EGL surface.
 */
AX_API void
axInit(void);

/**
 * @brief Shut down the platform subsystem and release all resources.
 *
 * After this call no other AX_* function may be called until #AX_Init is
 * called again.
 */
AX_API void
axShutdown(void);

/** @} */

/**
 * @defgroup logging Logging
 * @brief Optional file-based diagnostic logging.
 * @{
 */

/**
 * @brief Set or clear the process-wide platform log file.
 *
 * Passing a non-empty path opens that file in append mode and routes future
 * #axLog calls to it. Passing `NULL` or an empty string disables logging and
 * closes any previously opened log file.
 *
 * @param path  Absolute or relative log-file path, or `NULL` to disable logging.
 * @return `TRUE` if the new setting was applied, `FALSE` if the file could not
 *         be opened.
 */
AX_API bool_t
axSetLogFile(char const *path);

/**
 * @brief Return the currently configured log-file path.
 *
 * Returns an empty string when file logging is disabled.
 *
 * @return Null-terminated path string owned by the platform layer.
 */
AX_API char const *
axGetLogFile(void);

/**
 * @brief Write a formatted line to the configured log file.
 *
 * If no log file is configured, this function is a no-op.
 * The platform prepends a millisecond timestamp and appends a trailing newline.
 *
 * @param fmt  `printf`-style format string.
 */
AX_API void
axLog(char const *fmt, ...);

/**
 * @brief Flush buffered output for the current log file.
 */
AX_API void
axLogFlush(void);

/** @} */

/**
 * @defgroup system System utilities
 * @brief Timing, theme detection, and platform identification.
 * @{
 */

/**
 * @brief Return the current time as a millisecond-resolution monotonic timestamp.
 *
 * The epoch is unspecified; use the difference between two calls to measure
 * elapsed time.
 *
 * @return Milliseconds since an arbitrary fixed point.
 */
AX_API longTime_t
axGetMilliseconds(void);

/**
 * @brief Suspend the calling thread for at least @p msec milliseconds.
 *
 * @param msec  Sleep duration in milliseconds.  A value of 0 yields the CPU.
 */
AX_API void
axSleep(longTime_t msec);

/**
 * @brief Test whether the system is currently using a dark colour scheme.
 *
 * On macOS, queries `NSApp effectiveAppearance`.  On Wayland, checks the
 * `GTK_THEME` / `QT_STYLE_OVERRIDE` environment variables.  Returns `FALSE`
 * on platforms without theme detection (QNX, WebGL).
 *
 * @return `TRUE` if a dark theme is active, `FALSE` otherwise.
 */
AX_API bool_t
axIsDarkTheme(void);

/**
 * @brief Return a short string identifying the current platform.
 *
 * @return One of `"macos"`, `"linux (wayland)"`, `"linux (x11)"`, `"qnx"`, or `"webgl"`.
 *         The returned pointer is valid for the lifetime of the process.
 */
AX_API char const *
axGetPlatform(void);

/**
 * @brief Return the per-user settings (application support) directory.
 *
 * The directory is created if it does not already exist.  The returned path
 * does not include a trailing slash.
 *
 * @return Null-terminated UTF-8 path string.  Valid for the lifetime of the
 *         process; do not free.
 */
AX_API char const *
axSettingsDirectory(void);

/**
 * @brief Save an application settings blob under the settings directory.
 *
 * The @p name is treated as a file name relative to #axSettingsDirectory.
 * Existing file contents are replaced.
 *
 * @param name  Relative settings file name (for example, "browser.ini").
 * @param data  Pointer to bytes to write.
 * @param size  Number of bytes to write.
 * @return `TRUE` on success, `FALSE` on error.
 */
AX_API bool_t
axSettingsSave(char const *name, void const *data, size_t size);

/**
 * @brief Load an application settings blob from the settings directory.
 *
 * The @p name is treated as a file name relative to #axSettingsDirectory.
 *
 * @param name      Relative settings file name (for example, "browser.ini").
 * @param data      Destination buffer.
 * @param capacity  Destination buffer size in bytes.
 * @param out_size  Optional; receives bytes read on success.
 * @return `TRUE` on success, `FALSE` on error or when the file is larger than
 *         @p capacity.
 */
AX_API bool_t
axSettingsLoad(char const *name, void *data, size_t capacity, size_t *out_size);

/**
 * @brief Return the directory that contains shared read-only application data.
 *
 * On macOS this is `<bundle>/Contents/Resources/`; on Linux it is
 * `<exe>/../share/<appname>/`.
 *
 * @return Null-terminated UTF-8 path string.  Valid for the lifetime of the
 *         process; do not free.
 */
AX_API char const *
axShareDirectory(void);

/**
 * @brief Return the directory that contains platform-specific dynamic libraries.
 *
 * On macOS this is the same as #AX_ShareDirectory; on Linux it is
 * `<exe>/../lib/<appname>/`.
 *
 * @return Null-terminated UTF-8 path string.  Valid for the lifetime of the
 *         process; do not free.
 */
AX_API char const *
axLibDirectory(void);

/**
 * @defgroup filesystem Directory and filesystem utilities
 * @brief Portable directory creation, enumeration, and navigation.
 *
 * These functions replace raw POSIX calls (`opendir`/`readdir`/`mkdir`/
 * `getcwd`) or Win32 equivalents with a single cross-platform API.
 * @{
 */

/**
 * @brief Descriptor for a single directory entry returned by #axListDir.
 */
typedef struct {
  char   name[256];    /**< Entry name (not full path), null-terminated UTF-8. */
  bool_t is_directory; /**< `TRUE` if the entry is a subdirectory. */
  bool_t is_hidden;    /**< `TRUE` if the entry is hidden (dot-file on POSIX,
                            `FILE_ATTRIBUTE_HIDDEN` on Windows). */
  size_t size;         /**< File size in bytes; 0 for directories. */
  time_t modified;     /**< Last-modification time as a UTC Unix timestamp. */
} AXdirent;

/**
 * @brief Callback invoked once per entry by #axListDir.
 *
 * @param entry     Pointer to the current entry descriptor.  The pointer is
 *                  only valid for the duration of the callback.
 * @param userdata  Opaque value passed through from #axListDir.
 * @return `TRUE` to continue iteration; `FALSE` to stop early.
 */
typedef bool_t (*AXDirCallback)(AXdirent const *entry, void *userdata);

/**
 * @brief Create a directory at @p path.
 *
 * On POSIX the directory is created with mode 0777 (modified by umask).
 * Succeeds silently when the directory already exists.
 *
 * @param path  Null-terminated UTF-8 path of the directory to create.
 * @return `TRUE` on success or if the directory already exists; `FALSE`
 *         if creation failed (e.g. missing parent, permission denied).
 */
AX_API bool_t
axMkDir(char const *path);

/**
 * @brief List the entries of a directory, calling @p cb for each one.
 *
 * The special entries `.` and `..` are always skipped.  The order in which
 * entries are reported is platform-defined.
 *
 * @param path      Null-terminated UTF-8 path of the directory to enumerate.
 * @param cb        Function called once per entry.  Return `FALSE` to abort.
 * @param userdata  Passed unchanged to every @p cb invocation.
 * @return `TRUE` if the directory was opened successfully (even when @p cb
 *         stopped iteration early), `FALSE` if the directory could not be
 *         opened.
 */
AX_API bool_t
axListDir(char const *path, AXDirCallback cb, void *userdata);

/**
 * @brief Check whether a filesystem path exists.
 *
 * Works for both files and directories.
 *
 * @param path  Null-terminated UTF-8 path to test.
 * @return `TRUE` when the path exists, otherwise `FALSE`.
 */
AX_API bool_t
axPathExists(char const *path);

/**
 * @brief Get the current working directory.
 *
 * @param[out] buf  Buffer that receives the null-terminated UTF-8 path.
 * @param sz        Size of @p buf in bytes.
 * @return `TRUE` on success, `FALSE` if the path could not be retrieved
 *         or the buffer is too small.
 */
AX_API bool_t
axGetCwd(char *buf, size_t sz);

/** @} */

/**
 * @defgroup dynlib Dynamic library loading
 * @brief Run-time loading and symbol lookup for shared libraries.
 *
 * The file-extension macro #AX_DYNLIB_EXT provides the platform-specific
 * suffix so callers can construct library names portably:
 * @code
 *   void *lib = axDynlibOpen("myplugin" AX_DYNLIB_EXT);
 * @endcode
 * @{
 */

/**
 * @brief Platform-specific dynamic library file extension, including the dot.
 *
 * Expands to `".dll"` on Windows, `".dylib"` on macOS, and `".so"` on
 * Linux and QNX.
 */
#if defined(_WIN32) || defined(__MINGW32__)
#  define AX_DYNLIB_EXT ".dll"
#elif defined(__APPLE__)
#  define AX_DYNLIB_EXT ".dylib"
#else
#  define AX_DYNLIB_EXT ".so"
#endif

/**
 * @brief Open a shared library at @p path.
 *
 * @param path  Null-terminated path to the library file.
 * @return An opaque library handle on success, `NULL` on failure.
 *         Call #axDynlibError to obtain a human-readable error message.
 */
AX_API void *
axDynlibOpen(char const *path);

/**
 * @brief Look up a symbol by name in a library handle.
 *
 * @param handle  Handle returned by #axDynlibOpen.
 * @param sym     Null-terminated symbol name.
 * @return Pointer to the symbol, or `NULL` if not found.
 */
AX_API void *
axDynlibSym(void *handle, char const *sym);

/**
 * @brief Close a library handle.
 *
 * Passing `NULL` is safe and has no effect.
 *
 * @param handle  Handle returned by #axDynlibOpen, or `NULL`.
 */
AX_API void
axDynlibClose(void *handle);

/**
 * @brief Return a human-readable description of the last loading error.
 *
 * @return Null-terminated error string, or `NULL` if no error has occurred.
 *         The pointer may be invalidated by the next call to any dynlib
 *         function.
 */
AX_API char const *
axDynlibError(void);

/** @} */

/** @} */

/**
 * @defgroup window Window management
 * @brief Creating and managing the application window and rendering surface.
 * @{
 */

/**
 * @defgroup windowflags Window creation flags
 * @brief Bit flags passed to #axCreateWindow to control window and rendering
 *        context behaviour.
 *
 * Pass 0 for the default behaviour (double-buffered, decorated, resizable,
 * visible, windowed).
 * @{
 */
enum
{
  /** Enable double buffering.  Already the default; specify explicitly for
   *  clarity or to pair with a future single-buffer flag. */
  AX_WINDOW_DOUBLEBUFFER = 1 << 0,

  /** Create the window in fullscreen mode. */
  AX_WINDOW_FULLSCREEN   = 1 << 1,

  /** Create the window without title bar or OS decorations. */
  AX_WINDOW_BORDERLESS   = 1 << 2,

  /** Allow the window to be resized by the user.  When passing any nonzero
   *  @p flags to #axCreateWindow, include this flag explicitly to keep the
   *  window resizable; omitting it while using other flags will produce a
   *  fixed-size window on most platforms.  With @p flags == 0 the window
   *  remains resizable for backward compatibility. */
  AX_WINDOW_RESIZABLE    = 1 << 3,

  /** Create the window in a hidden state.  Call the platform show/focus API
   *  later to make it visible. */
  AX_WINDOW_HIDDEN       = 1 << 4,

  /** Request a high-DPI / Retina-resolution surface where available. */
  AX_WINDOW_HIGHDPI      = 1 << 5,
};
/** @} */

/**
 * @brief Create the main application window.
 *
 * Only one window is supported at a time.  Call #AX_Init before this
 * function.
 *
 * @param title   Null-terminated UTF-8 window title string.
 * @param width   Initial client-area width in logical pixels.
 * @param height  Initial client-area height in logical pixels.
 * @param flags   Combination of #AX_WINDOW_DOUBLEBUFFER, #AX_WINDOW_FULLSCREEN,
 *                #AX_WINDOW_BORDERLESS, #AX_WINDOW_RESIZABLE, #AX_WINDOW_HIDDEN,
 *                and #AX_WINDOW_HIGHDPI, or 0 for defaults.
 * @return `TRUE` on success, `FALSE` if window creation failed.
 */
AX_API bool_t
axCreateWindow(char const *title, uint32_t width, uint32_t height, uint32_t flags);

/**
 * @brief Create an off-screen rendering surface.
 *
 * On macOS this allocates an IOSurface-backed framebuffer.  On other
 * platforms that do not support off-screen surfaces this returns `FALSE`.
 *
 * @param width   Surface width in pixels.
 * @param height  Surface height in pixels.
 * @return `TRUE` on success, `FALSE` if off-screen surfaces are unsupported.
 */
AX_API bool_t
axCreateSurface(uint32_t width, uint32_t height);

/**
 * @brief Return the display scaling factor (HiDPI / Retina multiplier).
 *
 * Multiply logical sizes by this value to obtain physical pixel counts.
 * Returns 1.0 on platforms without scaling support.
 *
 * @return Display scale factor (e.g. 2.0 on a Retina display).
 */
AX_API float
axGetScaling(void);

/**
 * @brief Resize the window to the specified logical dimensions.
 *
 * @param width    New client-area width in logical pixels.
 * @param height   New client-area height in logical pixels.
 * @param centered If `TRUE`, re-centre the window on screen after resizing.
 * @return `TRUE` on success, `FALSE` if the operation is unsupported.
 */
AX_API bool_t
axSetSize(uint32_t width, uint32_t height, bool_t centered);

/**
 * @brief Query the current window size.
 *
 * @param[out] size  If non-NULL, receives the current width and height.
 * @return MAKEDWORD(width, height) packed into a single 32-bit value.
 */
AX_API uint32_t
axGetSize(struct AXsize* size);

/**
 * @brief Wait for the next event or until @p msec milliseconds have elapsed.
 *
 * On QNX the calling thread blocks on a POSIX semaphore that is signalled by
 * #AX_PostMessageW, so the wait is woken immediately when a new event is
 * posted from any thread.  Pass 0 to block indefinitely.
 *
 * @param msec  Maximum time to wait in milliseconds, or 0 to block indefinitely.
 * @return 1 if at least one event is ready, 0 if the timeout expired.
 */
AX_API int
axWaitEvent(longTime_t msec);

/**
 * @brief Make the platform OpenGL/EGL context current on the calling thread.
 *
 * Must be called before issuing any OpenGL commands.  On macOS this binds
 * the NSOpenGLContext; on Wayland/QNX it calls eglMakeCurrent.
 */
AX_API void
axMakeCurrentContext(void);

/**
 * @brief Begin a frame: make the context current and flush pending events.
 *
 * Equivalent to calling #AX_MakeCurrentContext followed by a display flush
 * on Wayland, or dispatching the Cocoa run loop on macOS.
 */
AX_API void
axBeginPaint(void);

/**
 * @brief End a frame: swap buffers and flush the display connection.
 *
 * Presents the rendered frame to the screen by swapping the EGL/OpenGL
 * back-buffer.  Also ensures the alpha channel is opaque where required
 * by compositors.
 */
AX_API void
axEndPaint(void);

/**
 * @brief Bind the platform's default framebuffer.
 *
 * On macOS this binds the IOSurface-backed FBO; on EGL-based platforms the
 * default framebuffer (0) is always active, so this is a no-op.
 */
AX_API void
axBindFramebuffer(void);

/** @} */

/**
 * @defgroup input Input utilities
 * @brief Key-code to human-readable string conversion.
 * @{
 */

/**
 * @brief Convert a virtual key-code to a printable name string.
 *
 * For printable ASCII keys (33–126) returns a single-character string.
 * For special keys returns a descriptive name such as `"UPARROW"`.
 * Returns `"<UNKNOWN KEYNUM>"` for unrecognised codes and
 * `"<KEY NOT FOUND>"` when @p keynum is `(uint32_t)-1`.
 *
 * @param keynum  Virtual key code (one of the `AX_KEY_*` constants or a
 *                printable ASCII value).
 * @return Null-terminated ASCII string.  Valid until the next call to this
 *         function (uses an internal static buffer for single-character names).
 */
AX_API char const *
axKeynumToString(uint32_t keynum);

/** @} */

/**
 * @defgroup joystick Joystick / gamepad support
 * @brief Functions for enumerating and reading joystick / gamepad devices.
 *
 * Joystick events are delivered through the standard event queue via
 * #axPollEvent.  The event fields carry the device data as follows:
 *
 * - #kEventJoyAxisMotion:  @p wParam = axis index (0-based),
 *                          @p lParam = axis value as @c int16_t cast to @c void*.
 * - #kEventJoyButtonDown / #kEventJoyButtonUp:
 *                          @p wParam = button index (0-based).
 * @{
 */

/**
 * @brief Initialise the joystick subsystem and open the first available device.
 *
 * @return `TRUE` if a joystick was found and opened, `FALSE` otherwise.
 *         Returning `FALSE` is not fatal; the application can continue without
 *         joystick input.
 */
AX_API bool_t
axJoystickInit(void);

/**
 * @brief Shut down the joystick subsystem and close any open device.
 */
AX_API void
axJoystickShutdown(void);

/**
 * @brief Check whether a joystick is currently connected and open.
 *
 * @return `TRUE` if a device is available, `FALSE` otherwise.
 */
AX_API bool_t
axJoystickAvailable(void);

/**
 * @brief Return the name of the connected joystick.
 *
 * @return Null-terminated UTF-8 device name, or `NULL` if no device is open.
 *         The pointer is valid until the next call to #AX_JoystickShutdown.
 */
AX_API char const *
axJoystickGetName(void);

/** @} */

/**
 * @defgroup swap Swap interval
 * @brief VSync / swap-interval control.
 * @{
 */

/**
 * @brief Set the OpenGL swap interval (VSync control).
 *
 * @param interval  0 = presentation is not synchronised to the display
 *                  refresh (no VSync); 1 = synchronise to every refresh.
 * @return `TRUE` on success, `FALSE` if the operation is unsupported on the
 *         current platform.
 */
AX_API bool_t
axSetSwapInterval(int interval);

/** @} */

/**
 * @defgroup timer Timer support
 * @brief Recurring and one-shot timer events delivered via the event queue.
 *
 * Timers post a #kEventTimer event when they fire.  The event fields carry:
 * - @p wParam = timer ID (the value returned by #AX_SetTimer).
 * - @p lParam = the @p userdata pointer passed to #AX_SetTimer.
 * @{
 */

/**
 * @brief Create a timer that posts #kEventTimer when it fires.
 *
 * The timer event is posted to @p obj, matching the target used by
 * #AX_PostMessageW.  Calling #AX_RemoveFromQueue with the same @p obj will
 * both flush queued events *and* cancel all timers registered for that object.
 *
 * @param obj          Target object; passed as #AX_Message::target on fire.
 * @param interval_ms  Timer interval in milliseconds.
 * @param userdata     Passed back in #AX_Message::lParam when the timer fires.
 * @param repeat       `TRUE` for a recurring timer, `FALSE` for a one-shot.
 * @return Timer ID (> 0) on success, 0 on failure.
 */
AX_API uint32_t
axSetTimer(void* obj, uint32_t interval_ms, void* userdata, bool_t repeat);

/**
 * @brief Cancel an active timer.
 *
 * @param timer_id  ID returned by #AX_SetTimer.
 */
AX_API void
axCancelTimer(uint32_t timer_id);

/** @} */

/**
 * @defgroup net Networking
 * @brief Portable socket, DNS, TLS, and async I/O primitives for HTTP/HTTPS
 *        and general TCP/UDP networking.
 *
 * The API is transport-oriented: it exposes sockets, name resolution, a
 * readiness-polling helper, and an opaque TLS context.  Higher layers
 * (e.g. an HTTP client) are built on top of these primitives.
 *
 * ### Typical TCP client usage
 * @code
 *   axNetInit();
 *   int sock = axNetSocket(AX_NET_AF_IPV4, AX_NET_SOCK_TCP);
 *   axNetConnect(sock, "example.com", 443);
 *   AXtlsctx *tls = axTlsConnect(sock, "example.com");
 *   axTlsSend(tls, "GET / HTTP/1.0\r\n\r\n", 18);
 *   char buf[4096];
 *   int n = axTlsRecv(tls, buf, sizeof(buf));
 *   axTlsClose(tls);
 *   axNetClose(sock);
 *   axNetShutdown();
 * @endcode
 * @{
 */

/**
 * @defgroup netaf Address families
 * @brief Values for the @p af parameter of #axNetSocket.
 * @{
 */
enum {
  AX_NET_AF_IPV4 = 4, /**< IPv4 (AF_INET). */
  AX_NET_AF_IPV6 = 6, /**< IPv6 (AF_INET6). */
};
/** @} */

/**
 * @defgroup netsock Socket types
 * @brief Values for the @p type parameter of #axNetSocket.
 * @{
 */
enum {
  AX_NET_SOCK_TCP = 1, /**< Reliable, ordered byte stream (SOCK_STREAM). */
  AX_NET_SOCK_UDP = 2, /**< Unreliable datagrams (SOCK_DGRAM). */
};
/** @} */

/**
 * @defgroup netpoll Poll event flags
 * @brief Bitmask values for the @p events parameter and return value of
 *        #axNetPoll.
 * @{
 */
enum {
  AX_NET_POLL_READ  = 1 << 0, /**< Socket has data ready to read. */
  AX_NET_POLL_WRITE = 1 << 1, /**< Socket is ready for writing. */
  AX_NET_POLL_ERR   = 1 << 2, /**< Socket has a pending error. */
};
/** @} */

/**
 * @brief Opaque TLS session context returned by #axTlsConnect.
 *
 * Treat as an opaque handle; do not inspect or allocate directly.
 */
typedef struct AXtlsctx AXtlsctx;

/**
 * @brief Initialise the networking subsystem.
 *
 * Must be called once before any other `axNet*` or `axTls*` function.
 * On Windows this calls `WSAStartup`; on POSIX platforms it is a no-op
 * (but must still be called for portability).
 *
 * @return `TRUE` on success, `FALSE` on failure.
 */
AX_API bool_t
axNetInit(void);

/**
 * @brief Shut down the networking subsystem.
 *
 * Releases any resources acquired by #axNetInit.  On Windows this calls
 * `WSACleanup`; on POSIX platforms it is a no-op.
 */
AX_API void
axNetShutdown(void);

/**
 * @brief Create a new socket.
 *
 * @param af    Address family: #AX_NET_AF_IPV4 or #AX_NET_AF_IPV6.
 * @param type  Socket type: #AX_NET_SOCK_TCP or #AX_NET_SOCK_UDP.
 * @return Non-negative socket descriptor on success, -1 on failure.
 */
AX_API int
axNetSocket(int af, int type);

/**
 * @brief Close a socket and release its resources.
 *
 * Passing -1 is safe and has no effect.
 *
 * @param sock  Socket descriptor returned by #axNetSocket or #axNetAccept.
 */
AX_API void
axNetClose(int sock);

/**
 * @brief Switch a socket between blocking and non-blocking I/O modes.
 *
 * In non-blocking mode #axNetSend and #axNetRecv return immediately if no
 * data can be transferred; use #axNetWouldBlock to distinguish that case
 * from a real error.
 *
 * @param sock        Socket descriptor.
 * @param nonblocking `TRUE` for non-blocking, `FALSE` for blocking.
 * @return `TRUE` on success, `FALSE` on failure.
 */
AX_API bool_t
axNetSetNonBlocking(int sock, bool_t nonblocking);

/**
 * @brief Enable or disable the `SO_REUSEADDR` socket option.
 *
 * Enabling reuse allows a server to rebind the same address/port immediately
 * after a previous instance exits.
 *
 * @param sock   Socket descriptor.
 * @param reuse  `TRUE` to enable, `FALSE` to disable.
 * @return `TRUE` on success, `FALSE` on failure.
 */
AX_API bool_t
axNetSetReuseAddr(int sock, bool_t reuse);

/**
 * @brief Bind a socket to a local port on all interfaces.
 *
 * @param sock  Socket descriptor.
 * @param port  Local port number in host byte order.
 * @return `TRUE` on success, `FALSE` on failure.
 */
AX_API bool_t
axNetBind(int sock, uint16_t port);

/**
 * @brief Mark a bound socket as passive and set the connection backlog.
 *
 * @param sock     Socket descriptor (previously bound with #axNetBind).
 * @param backlog  Maximum length of the pending-connection queue.
 * @return `TRUE` on success, `FALSE` on failure.
 */
AX_API bool_t
axNetListen(int sock, int backlog);

/**
 * @brief Accept an incoming connection on a listening socket.
 *
 * @param sock  Listening socket descriptor.
 * @return New connected socket descriptor on success, -1 if no connection
 *         is pending (non-blocking) or on error.
 */
AX_API int
axNetAccept(int sock);

/**
 * @brief Resolve @p host and connect the socket to the resulting address.
 *
 * Accepts both numeric address literals (`"93.184.216.34"`, `"::1"`) and
 * DNS hostnames (`"example.com"`).  For non-blocking sockets the function
 * may return `FALSE` while the connection is still in progress; use
 * #axNetPoll with #AX_NET_POLL_WRITE to wait for completion.
 *
 * @param sock  Connected socket descriptor.
 * @param host  Null-terminated hostname or numeric address string.
 * @param port  Destination port in host byte order.
 * @return `TRUE` if the connection was initiated or completed successfully,
 *         `FALSE` on hard failure.
 */
AX_API bool_t
axNetConnect(int sock, char const *host, uint16_t port);

/**
 * @brief Send data on a socket.
 *
 * @param sock  Socket descriptor.
 * @param buf   Pointer to the data to send.
 * @param len   Number of bytes to send.
 * @return Number of bytes actually sent (>= 0), or -1 on error.
 *         In non-blocking mode a return value of 0 means the send buffer is
 *         full; check #axNetWouldBlock to confirm.
 */
AX_API int
axNetSend(int sock, void const *buf, int len);

/**
 * @brief Receive data from a socket.
 *
 * @param sock  Socket descriptor.
 * @param buf   Buffer to receive data into.
 * @param len   Size of @p buf in bytes.
 * @return Number of bytes received (> 0), 0 on orderly shutdown or if no
 *         data is available in non-blocking mode (check #axNetWouldBlock),
 *         or -1 on error.
 */
AX_API int
axNetRecv(int sock, void *buf, int len);

/**
 * @brief Test whether the last send/receive returned due to no data
 *        being available rather than a hard error.
 *
 * Call this immediately after #axNetSend or #axNetRecv returns 0 or -1 on a
 * non-blocking socket to distinguish EAGAIN / EWOULDBLOCK / WSAEWOULDBLOCK
 * from a real error. This function reads thread-local state (errno on POSIX,
 * WSAGetLastError() on Windows), which is volatile. Calling any other function
 * that may modify network state before this function will give incorrect results.
 *
 * @return `TRUE` if the last network call would have blocked, `FALSE`
 *         otherwise.
 */
AX_API bool_t
axNetWouldBlock(void);

/**
 * @brief Resolve a hostname to a numeric IP address string.
 *
 * Uses `getaddrinfo` internally; the result prefers IPv4 addresses.
 *
 * @param host    Null-terminated hostname to resolve.
 * @param out     Buffer that receives the null-terminated address string.
 * @param outlen  Size of @p out in bytes.
 * @return `TRUE` if resolution succeeded and the result fits in @p out,
 *         `FALSE` otherwise.
 */
AX_API bool_t
axNetResolve(char const *host, char *out, int outlen);

/**
 * @brief Wait for I/O readiness on a single socket.
 *
 * Equivalent to a single-fd `poll()` / `select()` call.
 *
 * @param sock        Socket descriptor to monitor.
 * @param events      Bitmask of #AX_NET_POLL_READ, #AX_NET_POLL_WRITE,
 *                    and/or #AX_NET_POLL_ERR to wait for.
 * @param timeout_ms  Maximum time to wait in milliseconds.  Pass 0 to
 *                    return immediately without blocking.
 * @return Bitmask of the ready event flags, 0 on timeout, or -1 on error.
 */
AX_API int
axNetPoll(int sock, int events, int timeout_ms);

/**
 * @brief Perform a TLS client handshake on an already-connected socket.
 *
 * On success the returned context owns the TLS session.  All subsequent
 * I/O on that connection must go through #axTlsSend and #axTlsRecv rather
 * than #axNetSend and #axNetRecv.
 *
 * Platform backends:
 * - macOS: Secure Transport (Security.framework)
 * - Linux: OpenSSL (requires `-DHAVE_OPENSSL` and `-lssl -lcrypto`)
 * - Windows: Schannel (Security Support Provider Interface)
 * - WebGL: not supported (returns `NULL`)
 *
 * @param sock      Connected socket descriptor.
 * @param hostname  Server hostname used for SNI and certificate verification.
 * @return Opaque TLS context on success, `NULL` on failure or if TLS is
 *         not supported on the current platform.
 */
AX_API AXtlsctx *
axTlsConnect(int sock, char const *hostname);

/**
 * @brief Send a TLS close_notify alert and free the TLS context.
 *
 * The caller is still responsible for closing the underlying socket with
 * #axNetClose after this call.
 *
 * @param ctx  TLS context returned by #axTlsConnect.  Passing `NULL` is
 *             safe and has no effect.
 */
AX_API void
axTlsClose(AXtlsctx *ctx);

/**
 * @brief Send data through a TLS session.
 *
 * @param ctx  TLS context returned by #axTlsConnect.
 * @param buf  Pointer to the data to send.
 * @param len  Number of bytes to send.
 * @return Number of bytes sent, or -1 on error.
 */
AX_API int
axTlsSend(AXtlsctx *ctx, void const *buf, int len);

/**
 * @brief Receive data through a TLS session.
 *
 * @param ctx  TLS context returned by #axTlsConnect.
 * @param buf  Buffer to receive data into.
 * @param len  Size of @p buf in bytes.
 * @return Number of bytes received (> 0), 0 on orderly TLS shutdown, or
 *         -1 on error.
 */
AX_API int
axTlsRecv(AXtlsctx *ctx, void *buf, int len);

/** @} */

#endif
