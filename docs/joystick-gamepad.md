# Joystick & Gamepad

The library detects the first connected joystick or gamepad and delivers axis
and button changes as events through the normal `axPeekMessage` queue.

On **Windows** XInput (Xbox-compatible controllers) is used.  On **Linux** the
`/dev/input/js0` joystick device is read.  Other platforms expose the same API
but may not have a backend yet.

---

## Initialisation

```c
axInit();
axCreateWindow("Game", 1280, 720, AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_RESIZABLE);

if (axJoystickInit()) {
    printf("controller: %s\n", axJoystickGetName());
} else {
    printf("no controller found, continuing without\n");
}
```

`axJoystickInit` returns `FALSE` when no device is present — that is not a
fatal error.  You can check `axJoystickAvailable()` at any time to see
whether a device is still connected.

```c
axJoystickShutdown();   /* release the device at exit */
```

---

## Events

All joystick events carry data in `wParam` and `lParam`:

### Axis motion

```c
case kEventJoyAxisMotion: {
    int axis  = (int)msg.wParam;
    int value = (int)(intptr_t)msg.lParam; /* -32768 … 32767 */
    move_character(axis, value);
    break;
}
```

Axis indices on Windows (XInput):

| Index | Axis |
|------:|------|
| 0 | Left stick X |
| 1 | Left stick Y |
| 2 | Right stick X |
| 3 | Right stick Y |
| 4 | Left trigger |
| 5 | Right trigger |

Values range from −32 768 to +32 767.  Apply a dead-zone before use:

```c
static int apply_deadzone(int v, int dead) {
    if (v > -dead && v < dead) return 0;
    return v;
}

int lx = apply_deadzone((int)(intptr_t)msg.lParam, 4096);
```

### Button events

```c
case kEventJoyButtonDown:
    if (msg.wParam == 10)   /* A button on Xbox controller */
        jump();
    break;

case kEventJoyButtonUp:
    if (msg.wParam == 10)
        end_jump();
    break;
```

Button indices on Windows (XInput):

| Index | Button |
|------:|--------|
| 0  | D-pad Up |
| 1  | D-pad Down |
| 2  | D-pad Left |
| 3  | D-pad Right |
| 4  | Start |
| 5  | Back / Select |
| 6  | Left thumb click |
| 7  | Right thumb click |
| 8  | Left shoulder |
| 9  | Right shoulder |
| 10 | A |
| 11 | B |
| 12 | X |
| 13 | Y |

---

## Full example

```c
axInit();
axCreateWindow("Gamepad Demo", 800, 600,
    AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_RESIZABLE);

bool_t has_joy = axJoystickInit();
if (has_joy)
    printf("Using: %s\n", axJoystickGetName());

struct AXmessage msg;
int running = 1;
float player_x = 400, player_y = 300;

while (running) {
    axWaitEvent(16);
    while (axPeekMessage(&msg)) {
        switch (msg.message) {
        case kEventWindowClosed:
            running = 0;
            break;
        case kEventJoyAxisMotion:
            if (msg.wParam == 0)   /* left stick X */
                player_x += (int)(intptr_t)msg.lParam / 8192.0f;
            if (msg.wParam == 1)   /* left stick Y */
                player_y += (int)(intptr_t)msg.lParam / 8192.0f;
            break;
        case kEventJoyButtonDown:
            if (msg.wParam == 10) /* A */
                player_shoot();
            break;
        }
    }
    axBeginPaint();
    draw_player(player_x, player_y);
    axEndPaint();
}

if (has_joy) axJoystickShutdown();
axShutdown();
```
