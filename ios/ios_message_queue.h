#ifndef AX_IOS_MESSAGE_QUEUE_H
#define AX_IOS_MESSAGE_QUEUE_H

#include "../platform.h"

void ios_enqueue(struct AXmessage value);
// The host may use a generated paint message alongside the native window event.
void ios_set_paint_message(uint32_t message);
void ios_post_display_paint(uint32_t size);
// Freeze the readable queue at the start of a UIKit display callback.
void ios_begin_message_frame(void);
int ios_messages_ready(void);
void ios_remove_messages(void *target);
void ios_shutdown_messages(void);

#endif
