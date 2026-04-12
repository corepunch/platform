#ifndef __EVENTS_H__
#define __EVENTS_H__

#define kEventLeftMouseDown 0xfac0b5e7
typedef struct AXmessage* LeftMouseDownEventPtr;

#define kEventRightMouseDown 0x1057ac50
typedef struct AXmessage* RightMouseDownEventPtr;

#define kEventOtherMouseDown 0x9822ca20
typedef struct AXmessage* OtherMouseDownEventPtr;

#define kEventLeftMouseUp 0xf73e019e
typedef struct AXmessage* LeftMouseUpEventPtr;

#define kEventRightMouseUp 0x9160ff69
typedef struct AXmessage* RightMouseUpEventPtr;

#define kEventOtherMouseUp 0x567302d9
typedef struct AXmessage* OtherMouseUpEventPtr;

#define kEventLeftMouseDragged 0x088e1f1b
typedef struct AXmessage* LeftMouseDraggedEventPtr;

#define kEventRightMouseDragged 0x29d4da42
typedef struct AXmessage* RightMouseDraggedEventPtr;

#define kEventOtherMouseDragged 0x0ae3dd32
typedef struct AXmessage* OtherMouseDraggedEventPtr;

#define kEventLeftDoubleClick 0x5a92bc67
typedef struct AXmessage* LeftDoubleClickEventPtr;

#define kEventRightDoubleClick 0xeeebbe60
typedef struct AXmessage* RightDoubleClickEventPtr;

#define kEventOtherDoubleClick 0xf6c60630
typedef struct AXmessage* OtherDoubleClickEventPtr;

#define kEventMouseMoved 0x65db8b6f
typedef struct AXmessage* MouseMovedEventPtr;

#define kEventScrollWheel 0x626f90e3
typedef struct AXmessage* ScrollWheelEventPtr;

#define kEventDragDrop 0x25989e7a
typedef struct AXmessage* DragDropEventPtr;

#define kEventDragEnter 0xc0e97a77
typedef struct AXmessage* DragEnterEventPtr;

#define kEventKeyDown 0x83b19b78
typedef struct AXmessage* KeyDownEventPtr;

#define kEventKeyUp 0xfca37d71
typedef struct AXmessage* KeyUpEventPtr;

#define kEventChar 0x2879e23d
typedef struct AXmessage* CharEventPtr;

#define kEventWindowPaint 0x7ef9e53b
#define kEventWindowClosed 0x7268e69d
#define kEventWindowResized 0xa216e847
#define kEventWindowChangedScreen 0x5fe6b4bf
#define kEventKillFocus 0xa7c0f8d7
typedef void* KillFocusEventPtr;

#define kEventSetFocus 0xc399d265
typedef void* SetFocusEventPtr;

#define kEventJoyAxisMotion 0x93e5a71b
typedef struct AXmessage* JoyAxisMotionEventPtr;

#define kEventJoyButtonDown 0x6d4f8e2a
typedef struct AXmessage* JoyButtonDownEventPtr;

#define kEventJoyButtonUp 0x8b1c5f3d
typedef struct AXmessage* JoyButtonUpEventPtr;

#define kEventTimer 0xa8f3b521
typedef struct AXmessage* TimerEventPtr;

#endif
