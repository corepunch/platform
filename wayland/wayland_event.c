#include "../platform.h"

#include "wayland_local.h"
#include <unistd.h>
#include <poll.h>

static struct
{
  EVENT queue[0x10000];
  WORD write, read;
  VECTOR2D pointer;
} events = { 0 };

extern struct xkb_state* xkb_state;

static struct wl_pointer* pointer = NULL;
static struct wl_keyboard* keyboard = NULL;

static void
pointer_enter(void* data,
              struct wl_pointer* pointer,
              uint32_t serial,
              struct wl_surface* surface,
              wl_fixed_t sx,
              wl_fixed_t sy)
{
}

static void
pointer_leave(void* data,
              struct wl_pointer* pointer,
              uint32_t serial,
              struct wl_surface* surface)
{
}

static void
pointer_motion(void* data,
               struct wl_pointer* pointer,
               uint32_t time,
               wl_fixed_t sx,
               wl_fixed_t sy)
{
  events.pointer.x = wl_fixed_to_double(sx);
  events.pointer.y = wl_fixed_to_double(sy);
  events.queue[events.write++] = (EVENT){ 
    .x = events.pointer.x,
    .y = events.pointer.y,
//    .lParam = (void*)(intptr_t)(((int16_t)sy)<<16|(int16_t)sx),
    .message = kEventMouseMoved 
  };
}

static void
pointer_button(void* data,
               struct wl_pointer* pointer,
               uint32_t serial,
               uint32_t time,
               uint32_t button,
               uint32_t state)
{
  bool_t pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
  switch (button) {
  case 271:
    events.queue[events.write++] = (EVENT){ 
      .x = events.pointer.x,
      .y = events.pointer.y,
      .message = pressed ? kEventLeftMouseDown : kEventLeftMouseUp
    };
    break;
  case 272:
    events.queue[events.write++] = (EVENT){ 
      .x = events.pointer.x,
      .y = events.pointer.y,
      .message = pressed ? kEventRightMouseDown : kEventRightMouseUp
    };
    break;
  }
}

static void
pointer_axis(void* data,
             struct wl_pointer* pointer,
             uint32_t time,
             uint32_t axis,
             wl_fixed_t value)
{
  int16_t dx = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? 0 : wl_fixed_to_double(value);
  int16_t dy = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? wl_fixed_to_double(value) : 0;
  events.queue[events.write++] = (EVENT) {
    .x = events.pointer.x,
    .y = events.pointer.y,
    .lParam = (void*)(intptr_t)((dy)<<16|dx),
    .message = kEventScrollWheel
  };
}

// Attach pointer listener
static struct wl_pointer_listener pointer_listener = {
  .enter = pointer_enter,
  .leave = pointer_leave,
  .motion = pointer_motion,
  .button = pointer_button,
  .axis = pointer_axis,
};

static void
keyboard_keymap(void* data,
                struct wl_keyboard* keyboard,
                uint32_t format,
                int fd,
                uint32_t size)
{
  close(fd);
}

static void
keyboard_enter(void* data,
               struct wl_keyboard* keyboard,
               uint32_t serial,
               struct wl_surface* surface,
               struct wl_array* keys)
{
  printf("Keyboard focus entered\n");
}

static void
keyboard_leave(void* data,
               struct wl_keyboard* keyboard,
               uint32_t serial,
               struct wl_surface* surface)
{
  printf("Keyboard focus left\n");
}

static void
keyboard_key(void* data,
             struct wl_keyboard* keyboard,
             uint32_t serial,
             uint32_t time,
             uint32_t key,
             uint32_t state)
{
  if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state, key + 8);
    uint32_t utf32 = xkb_keysym_to_utf32(keysym);
    if (utf32) {
      if (utf32 >= 0x21 && utf32 <= 0x7E) {
        events.queue[events.write++] = (EVENT){
          .message = kEventKeyDown,
          .wParam = key,
          .lParam = (void*)(intptr_t)utf32,
        };
        // printf ("the key %c was pressed\n", (char)utf32);
        // if (utf32 == 'q') running = 0;
      } else {
        printf("the key U+%04X was pressed\n", utf32);
      }
    } else {
      char name[64];
      xkb_keysym_get_name(keysym, name, 64);
      PEVENT e = &events.queue[events.write++];
      *e = (EVENT){
        .message = kEventKeyDown,
        .wParam = key,
      };
      for (int i = 0; i < sizeof(name); i++) {
        name[i] = tolower(name[i]);
      }
      strncpy(e->lParam, name, sizeof(e->lParam));
      printf("the key %s was pressed\n", name);
    }
  }
}

static void
keyboard_modifiers(void* data,
                   struct wl_keyboard* keyboard,
                   uint32_t serial,
                   uint32_t mods_depressed,
                   uint32_t mods_latched,
                   uint32_t mods_locked,
                   uint32_t group)
{
  printf("Modifiers changed: depressed=%u, latched=%u, locked=%u, group=%u\n",
         mods_depressed,
         mods_latched,
         mods_locked,
         group);
}

// Attach keyboard listener
static struct wl_keyboard_listener keyboard_listener = {
  .keymap = keyboard_keymap,
  .enter = keyboard_enter,
  .leave = keyboard_leave,
  .key = keyboard_key,
  .modifiers = keyboard_modifiers,
};

static void
seat_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities)
{
  if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
    pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(pointer, &pointer_listener, &events);
    printf("Pointer device found!\n");
  }
  if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
    keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, &events);
    printf("Keyboard device found!\n");
  }
}

static struct wl_seat_listener seat_listener = {
  .capabilities = seat_capabilities,
};

struct wl_seat_listener*
get_seat_listener(void)
{
  return &seat_listener;
}

static void
xdg_surface_configure(void* data,
                      struct xdg_surface* xdg_surface,
                      uint32_t serial)
{
  xdg_surface_ack_configure(xdg_surface, serial);
  printf("xdg_surface configured\n");
}

static void
xdg_surface_close(void* data, struct xdg_surface* xdg_surface)
{
  printf("xdg_surface closed\n");
}

static void
xdg_surface_commit(void* data, struct xdg_surface* xdg_surface)
{
  printf("xdg_surface committed\n");
}

static void
xdg_surface_focus(void* data, struct xdg_surface* xdg_surface)
{
  printf("xdg_surface focused\n");
}

static struct xdg_surface_listener xdg_surface_listener = {
  xdg_surface_configure,
  // xdg_surface_close,
  // xdg_surface_commit,
  // xdg_surface_focus
};

struct xdg_surface_listener*
get_xdg_surface_listener(void)
{
  return &xdg_surface_listener;
}

int
WI_WaitEvent(TIME time)
{
  extern struct wl_display* display;
  
  if (time > 0) {
    // Use poll or epoll to wait with timeout
    struct pollfd fds[1];
    fds[0].fd = wl_display_get_fd(display);
    fds[0].events = POLLIN;
    
    int ret = poll(fds, 1, time);
    if (ret > 0) {
      wl_display_dispatch(display);
      return 1;
    }
    return 0;
  }
  
  // No timeout, just dispatch pending events
  wl_display_dispatch_pending(display);
  return 0;
}

int
WI_PollEvent(PEVENT pEvent)
{
  if (events.read != events.write) {
    *pEvent = events.queue[events.read++];
    return 1;
  } else {
    return 0;
  }
}

void NotifyWindowEvent(void *window, uint32_t eventType, uint32_t wparam) {
  events.queue[events.write++] = (EVENT){ 
    .wParam = wparam,
    .message = kEventWindowPaint,
    .target = window,
  };
}

void
WI_PostMessageW(void* hobj, uint32_t event, uint32_t wparam, void* lparam)
{
  if (events.write - events.read >= sizeof(events.queue) / sizeof(events.queue[0])) {
    // Queue is full, ignore the message
    return;
  }
  events.queue[events.write++] = (EVENT){
    .target = hobj,
    .message = event,
    .wParam = wparam,
    .lParam = lparam,
  };
}

void
WI_RemoveFromQueue(void* hobj)
{
  WORD read_idx = events.read;
  WORD write_idx = events.write;
  WORD new_write = events.read;
  
  // Scan through the queue and keep events not matching hobj
  while (read_idx != write_idx) {
    if (events.queue[read_idx].target != hobj) {
      events.queue[new_write++] = events.queue[read_idx];
    }
    read_idx++;
  }
  
  events.write = new_write;
}

void
NotifyFileDropEvent(char const* filename, float x, float y)
{
  // File drop not fully implemented yet
  // Would need to allocate and copy filename string
  (void)filename;
  (void)x;
  (void)y;
}
