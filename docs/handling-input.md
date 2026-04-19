# Handling Input

All input arrives as `AXmessage` events via `axPollEvent`.  The `wParam`
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

`keyCode` is one of the `AX_KEY_*` constants below, or a printable ASCII value
(32–126) for alphanumeric keys.

### Checking modifiers

`modflags` is a bitmask ORed from:

| Flag | Meaning |
|------|---------|
| `AX_MOD_SHIFT` | Shift held |
| `AX_MOD_CTRL`  | Control held |
| `AX_MOD_ALT`   | Alt / Option held |
| `AX_MOD_CMD`   | Command / Super / Meta held |

```c
case kEventKeyDown:
    /* Ctrl+S → save */
    if (msg.modflags & AX_MOD_CTRL && msg.keyCode == 's')
        save_file();

    /* Shift+Tab → reverse focus */
    if (msg.modflags & AX_MOD_SHIFT && msg.keyCode == AX_KEY_TAB)
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
| `AX_KEY_TAB`       |   9 | Tab |
| `AX_KEY_ENTER`     |  13 | Return |
| `AX_KEY_ESCAPE`    |  27 | Escape |
| `AX_KEY_SPACE`     |  32 | Space |
| `AX_KEY_BACKSPACE` | 127 | Backspace |
| `AX_KEY_UPARROW`   | 128 | ↑ |
| `AX_KEY_DOWNARROW` | 129 | ↓ |
| `AX_KEY_LEFTARROW` | 130 | ← |
| `AX_KEY_RIGHTARROW`| 131 | → |
| `AX_KEY_ALT`       | 132 | Alt key itself |
| `AX_KEY_CTRL`      | 133 | Ctrl key itself |
| `AX_KEY_SHIFT`     | 134 | Shift key itself |
| `AX_KEY_F1`–`AX_KEY_F12` | 135–146 | Function keys |
| `AX_KEY_INS`       | 147 | Insert |
| `AX_KEY_DEL`       | 148 | Delete |
| `AX_KEY_PGDN`      | 149 | Page Down |
| `AX_KEY_PGUP`      | 150 | Page Up |
| `AX_KEY_HOME`      | 151 | Home |
| `AX_KEY_END`       | 152 | End |
| `AX_KEY_PAUSE`     | 255 | Pause/Break |
| `AX_KEY_MOUSE1`    | 200 | Left mouse button |
| `AX_KEY_MOUSE2`    | 201 | Right mouse button |
| `AX_KEY_MOUSE3`    | 202 | Middle mouse button |
| `AX_KEY_MWHEELUP`  | 240 | Wheel up |
| `AX_KEY_MWHEELDOWN`| 239 | Wheel down |

Printable ASCII characters (letters, digits, punctuation) use their ASCII
values directly — `'a'` is 97, `'Z'` is 90, etc.

`axKeynumToString(keyCode)` converts any code to a human-readable name.

---

## Mouse

### Position and clicks

Mouse position is available as `msg.x` / `msg.y` (logical pixels from the
top-left corner of the client area):

```c
case kEventLeftButtonDown:
    start_drag(msg.x, msg.y);
    break;

case kEventLeftButtonUp:
    end_drag(msg.x, msg.y);
    break;

case kEventMouseMoved:
    hover(msg.x, msg.y);
    break;
```

### Dragging

Drag events fire while a button is held and the cursor moves:

```c
case kEventLeftButtonDragged:
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
