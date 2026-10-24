/**
 * @file audio_backend.h
 * @brief Hardware playback hooks used by the shared WAV device.
 *
 * The shared device always feeds the backend signed 16-bit interleaved
 * frames in host byte order. Backends open the default output device and
 * call ax_audio_device_fill() whenever the hardware needs more samples.
 */
#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

#include <stdint.h>
#include "../platform.h"

void ax_audio_device_fill(void *dst, int nbytes);

bool_t ax_audio_hw_open(int freq, int channels, int frames);
void ax_audio_hw_close(void);
void ax_audio_hw_pause(bool_t pause);

#endif
