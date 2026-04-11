# Handling Input

All input arrives as `WI_Message` events via `WI_PollEvent`.  The `wParam`
union gives you either a packed `(x, y)` point for mouse events or a
`(keyCode, modflags)` pair for keyboard events.

---

## Keyboard

### Key-down and key-up

```c
case kEventKeyDown:
    printf("key %d pressed\n", msg.keyCode);
    break;

case kEventKeyUp:
    printf("key %d released\n", msg.keyCode);
    break;
```

`keyCode` is one of the `WI_KEY_*` constants below, or a printable ASCII value
(32–126) for alphanumeric keys.

### Checking modifiers

`modflags` is a bitmask ORed from:

| Flag | Meaning |
|------|---------|
| `WI_MOD_SHIFT` | Shift held |
| `WI_MOD_CTRL`  | Control held |
| `WI_MOD_ALT`   | Alt / Option held |
| `WI_MOD_CMD`   | Command / Super / Meta held |

```c
case kEventKeyDown:
    /* Ctrl+S → save */
    if (msg.modflags & WI_MOD_CTRL && msg.keyCode == 's')
        save_file();

    /* Shift+Tab → reverse focus */
    if (msg.modflags & WI_MOD_SHIFT && msg.keyCode == WI_KEY_TAB)
        focus_prev();
    break;
```

### Text input

For actual character input (respects keyboard layout, dead keys, IME) listen
for `kEventChar`.  The UTF-8 bytes are stored directly in `lParam`:

```c
case kEventChar: {
    char buf[sizeof(void *)];
    memcpy(buf, &msg.lParam, sizeof(buf));
    /* buf is a null-terminated UTF-8 string of at most one code point */
    append_text(buf);
    break;
}
```

Use `kEventKeyDown` for hotkeys and `kEventChar` for text fields.

### Key code table

| Constant | Value | Key |
|----------|------:|-----|
| `WI_KEY_TAB`       |   9 | Tab |
| `WI_KEY_ENTER`     |  13 | Return |
| `WI_KEY_ESCAPE`    |  27 | Escape |
| `WI_KEY_SPACE`     |  32 | Space |
| `WI_KEY_BACKSPACE` | 127 | Backspace |
| `WI_KEY_UPARROW`   | 128 | ↑ |
| `WI_KEY_DOWNARROW` | 129 | ↓ |
| `WI_KEY_LEFTARROW` | 130 | ← |
| `WI_KEY_RIGHTARROW`| 131 | → |
| `WI_KEY_ALT`       | 132 | Alt key itself |
| `WI_KEY_CTRL`      | 133 | Ctrl key itself |
| `WI_KEY_SHIFT`     | 134 | Shift key itself |
| `WI_KEY_F1`–`WI_KEY_F12` | 135–146 | Function keys |
| `WI_KEY_INS`       | 147 | Insert |
| `WI_KEY_DEL`       | 148 | Delete |
| `WI_KEY_PGDN`      | 149 | Page Down |
| `WI_KEY_PGUP`      | 150 | Page Up |
| `WI_KEY_HOME`      | 151 | Home |
| `WI_KEY_END`       | 152 | End |
| `WI_KEY_PAUSE`     | 255 | Pause/Break |
| `WI_KEY_MOUSE1`    | 200 | Left mouse button |
| `WI_KEY_MOUSE2`    | 201 | Right mouse button |
| `WI_KEY_MOUSE3`    | 202 | Middle mouse button |
| `WI_KEY_MWHEELUP`  | 240 | Wheel up |
| `WI_KEY_MWHEELDOWN`| 239 | Wheel down |

Printable ASCII characters (letters, digits, punctuation) use their ASCII
values directly — `'a'` is 97, `'Z'` is 90, etc.

`WI_KeynumToString(keyCode)` converts any code to a human-readable name.

---

## Mouse

### Position and clicks

Mouse position is available as `msg.x` / `msg.y` (logical pixels from the
top-left corner of the client area):

```c
case kEventLeftMouseDown:
    start_drag(msg.x, msg.y);
    break;

case kEventLeftMouseUp:
    end_drag(msg.x, msg.y);
    break;

case kEventMouseMoved:
    hover(msg.x, msg.y);
    break;
```

### Dragging

Drag events fire while a button is held and the cursor moves:

```c
case kEventLeftMouseDragged:
    /* msg.dx / msg.dy are the deltas since the last event */
    pan_camera(msg.dx, msg.dy);
    break;
```

### Scroll wheel

```c
case kEventScrollWheel:
    /* msg.dy: positive = scroll up, negative = scroll down */
    zoom += msg.dy * 0.1f;
    break;
```

### Double-click

```c
case kEventLeftDoubleClick:
    open_item_at(msg.x, msg.y);
    break;
```

All three buttons have `Down`, `Up`, `Dragged`, and `DoubleClick` variants
(`Left`, `Right`, `Other`).

---

## Drag and drop

When the user drops a file onto the window:

```c
case kEventDragEnter:
    highlight_drop_zone(TRUE);
    break;

case kEventDragDrop:
    highlight_drop_zone(FALSE);
    load_file((char *)msg.lParam);
    break;
```

`lParam` points to a null-terminated UTF-8 file path.  The pointer is valid
only for the duration of the event handler.
