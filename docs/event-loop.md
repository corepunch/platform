# The Event Loop

The platform library uses a lightweight FIFO queue.  Platform-specific backends
push events in, and your code pulls them out.

---

## Poll vs. wait

`axPeekMessage` returns immediately — `1` if an event was written into `msg`,
`0` if the queue is empty:

```c
struct AXmessage msg;
while (axPeekMessage(&msg)) {
    handle_event(&msg);
}
```

`axWaitEvent` blocks until an event arrives or the timeout expires.  It
returns `1` if there is at least one event ready, `0` on timeout:

```c
axWaitEvent(16);   /* sleep up to ~16 ms → ~60 FPS cap */
while (axPeekMessage(&msg)) {
    handle_event(&msg);
}
```

Pass `0` to `axWaitEvent` to block indefinitely until any event arrives —
useful for editor-style apps that do not need to re-draw every frame.

`axGetMessage` combines the two patterns and returns the next event,
blocking until one is available (except on WebGL, where blocking the browser
main thread is not supported):

```c
while (axGetMessage(&msg)) {
    handle_event(&msg);
}
```

### Typical game loop

```c
axInit();
axCreateWindow("Game", 1280, 720, AX_WINDOW_DOUBLEBUFFER | AX_WINDOW_RESIZABLE);

struct AXmessage msg;
int running = 1;

while (running) {
    axWaitEvent(16);

    while (axPeekMessage(&msg)) {
        switch (msg.message) {
        case kEventWindowClosed:
            running = 0;
            break;
        case kEventKeyDown:
            if (msg.keyCode == AX_KEY_ESCAPE) running = 0;
            game_key_down(msg.keyCode, msg.modflags);
            break;
        case kEventWindowResized:
            game_resize(LOWORD(msg.wParam), HIWORD(msg.wParam));
            break;
        }
    }

    game_update();

    axBeginPaint();
    game_render();
    axEndPaint();
}

axShutdown();
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

`axPostMessageW` pushes an event onto the queue from any thread:

```c
axPostMessageW(my_window, kEventWindowPaint, 0, NULL);
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
    axPostMessageW(NULL, MY_EVENT_WORK_DONE, result_code, NULL);
    return NULL;
}
```

---

## Timers

`axSetTimer` fires a `kEventTimer` on a recurring or one-shot schedule:

```c
/* 500 ms recurring timer */
uint32_t blink_timer = axSetTimer(NULL, 500, NULL, TRUE);

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
uint32_t splash_timer = axSetTimer(NULL, 3000, NULL, FALSE);
```

Cancel a timer explicitly with `axCancelTimer`:

```c
axCancelTimer(blink_timer);
```

`axRemoveFromQueue(target)` cancels **all** timers registered for that target
*and* flushes any queued events for it — handy when destroying an object:

```c
axRemoveFromQueue(closing_window);
```

---

## Cleaning up stale events

When you destroy a window or object, call `axRemoveFromQueue` with its handle
before freeing it to avoid dangling-pointer dereferences in the event loop:

```c
void destroy_panel(Panel *p) {
    axRemoveFromQueue(p);
    free(p);
}
```
