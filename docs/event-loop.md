# The Event Loop

The platform library uses a lightweight FIFO queue.  Platform-specific backends
push events in, and your code pulls them out.

---

## Poll vs. wait

`WI_PollEvent` returns immediately — `1` if an event was written into `msg`,
`0` if the queue is empty:

```c
struct WI_Message msg;
while (WI_PollEvent(&msg)) {
    handle_event(&msg);
}
```

`WI_WaitEvent` blocks until an event arrives or the timeout expires.  It
returns `1` if there is at least one event ready, `0` on timeout:

```c
WI_WaitEvent(16);   /* sleep up to ~16 ms → ~60 FPS cap */
while (WI_PollEvent(&msg)) {
    handle_event(&msg);
}
```

Pass `0` to `WI_WaitEvent` to block indefinitely until any event arrives —
useful for editor-style apps that do not need to re-draw every frame.

### Typical game loop

```c
WI_Init();
WI_CreateWindow("Game", 1280, 720, WI_WINDOW_DOUBLEBUFFER | WI_WINDOW_RESIZABLE);

struct WI_Message msg;
int running = 1;

while (running) {
    WI_WaitEvent(16);

    while (WI_PollEvent(&msg)) {
        switch (msg.message) {
        case kEventWindowClosed:
            running = 0;
            break;
        case kEventKeyDown:
            if (msg.keyCode == WI_KEY_ESCAPE) running = 0;
            game_key_down(msg.keyCode, msg.modflags);
            break;
        case kEventWindowResized:
            game_resize(LOWORD(msg.wParam), HIWORD(msg.wParam));
            break;
        }
    }

    game_update();

    WI_BeginPaint();
    game_render();
    WI_EndPaint();
}

WI_Shutdown();
```

---

## Window events

| Event | When | `wParam` |
|-------|------|---------|
| `kEventWindowPaint`   | Window needs repainting | — |
| `kEventWindowClosed`  | User closed the window  | — |
| `kEventWindowResized` | Size changed            | `MAKEDWORD(width, height)` |
| `kEventWindowChangedScreen` | Moved to another monitor | — |
| `kEventSetFocus`      | Window gained focus     | — |
| `kEventKillFocus`     | Window lost focus       | — |

```c
case kEventWindowResized: {
    uint32_t w = LOWORD(msg.wParam);
    uint32_t h = HIWORD(msg.wParam);
    glViewport(0, 0, w, h);
    projection = make_projection(w, h);
    break;
}
```

---

## Posting custom events

`WI_PostMessageW` pushes an event onto the queue from any thread:

```c
WI_PostMessageW(my_window, kEventWindowPaint, 0, NULL);
```

`kEventWindowPaint` and `kEventWindowResized` events are coalesced per target —
posting them repeatedly does not flood the queue.

Use any `uint32_t` value as the `event` parameter for your own messages.

### Cross-thread wake-up

Background threads can notify the main thread by posting any event:

```c
/* worker thread */
void *worker(void *arg) {
    do_heavy_work();
    WI_PostMessageW(NULL, MY_EVENT_WORK_DONE, result_code, NULL);
    return NULL;
}
```

---

## Timers

`WI_SetTimer` fires a `kEventTimer` on a recurring or one-shot schedule:

```c
/* 500 ms recurring timer */
uint32_t blink_timer = WI_SetTimer(NULL, 500, NULL, TRUE);

/* … in the event loop … */
case kEventTimer:
    if (msg.wParam == blink_timer)
        cursor_visible = !cursor_visible;
    break;
```

The timer ID is passed back in `msg.wParam`; the `userdata` pointer you
supplied is in `msg.lParam`.

```c
/* One-shot: hide splash after 3 s */
uint32_t splash_timer = WI_SetTimer(NULL, 3000, NULL, FALSE);
```

Cancel a timer explicitly with `WI_CancelTimer`:

```c
WI_CancelTimer(blink_timer);
```

`WI_RemoveFromQueue(target)` cancels **all** timers registered for that target
*and* flushes any queued events for it — handy when destroying an object:

```c
WI_RemoveFromQueue(closing_window);
```

---

## Cleaning up stale events

When you destroy a window or object, call `WI_RemoveFromQueue` with its handle
before freeing it to avoid dangling-pointer dereferences in the event loop:

```c
void destroy_panel(Panel *p) {
    WI_RemoveFromQueue(p);
    free(p);
}
```
