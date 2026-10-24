/*
 * test_audio.c - WAV loader and software mixer tests.
 *
 * Hardware playback is optional: CI hosts often have no audio device, so
 * axAudioOpen is allowed to fail. Loading, decoding, and mixing are not.
 */

#include "platform.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wr_le16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)(v >> 8);
}

static void wr_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
  p[2] = (uint8_t)((v >> 16) & 0xff);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t build_pcm16(uint8_t *dst, int frames, int channels, int freq)
{
  uint32_t data = (uint32_t)(frames * channels * 2);
  uint32_t riff = 36 + data;
  int i;
  wr_le32(dst, 0x46464952); /* RIFF */
  memcpy(dst, "RIFF", 4);
  wr_le32(dst + 4, riff);
  memcpy(dst + 8, "WAVE", 4);
  memcpy(dst + 12, "fmt ", 4);
  wr_le32(dst + 16, 16);
  wr_le16(dst + 20, 1);
  wr_le16(dst + 22, (uint16_t)channels);
  wr_le32(dst + 24, (uint32_t)freq);
  wr_le32(dst + 28, (uint32_t)(freq * channels * 2));
  wr_le16(dst + 32, (uint16_t)(channels * 2));
  wr_le16(dst + 34, 16);
  memcpy(dst + 36, "data", 4);
  wr_le32(dst + 40, data);
  for (i = 0; i < frames * channels; i++)
    wr_le16(dst + 44 + i * 2, (uint16_t)(i == 0 ? 1000 : -2000));
  return 44 + data;
}

static void test_pcm_and_mix(void)
{
  uint8_t file[128];
  uint32_t size = build_pcm16(file, 4, 1, 22050);
  AXaudiospec spec;
  uint8_t *buf = NULL;
  uint32_t len = 0;
  int16_t sample;
  int16_t dst[2];
  int16_t src[2];
  char path[] = "ax_test_tone.wav";
  FILE *f;

  assert(axLoadWAVMem(file, size, &spec, &buf, &len) == TRUE);
  assert(spec.freq == 22050);
  assert(spec.format == AX_AUDIO_S16);
  assert(spec.channels == 1);
  assert(len == 8);
  memcpy(&sample, buf, 2);
  assert(sample == 1000);
  axFreeWAV(buf);

  f = fopen(path, "wb");
  assert(f);
  assert(fwrite(file, 1, size, f) == size);
  fclose(f);
  assert(axLoadWAV(path, &spec, &buf, &len) == TRUE);
  assert(spec.freq == 22050);
  axFreeWAV(buf);
  remove(path);

  dst[0] = 1000;
  dst[1] = -1000;
  src[0] = 500;
  src[1] = 500;
  axAudioMix((uint8_t *)dst, (uint8_t *)src, AX_AUDIO_S16, sizeof(dst), 128);
  assert(dst[0] == 1500);
  assert(dst[1] == -500);
  axAudioMix((uint8_t *)dst, (uint8_t *)src, AX_AUDIO_S16, sizeof(dst), 0);
  assert(dst[0] == 1500);
}

static void test_mulaw_and_reject(void)
{
  uint8_t file[64];
  AXaudiospec spec;
  uint8_t *buf = NULL;
  uint32_t len = 0;
  int16_t sample;
  uint8_t bad[] = "not a wav file!!";

  memcpy(file, "RIFF", 4);
  wr_le32(file + 4, 36);
  memcpy(file + 8, "WAVE", 4);
  memcpy(file + 12, "fmt ", 4);
  wr_le32(file + 16, 16);
  wr_le16(file + 20, 7); /* mu-law */
  wr_le16(file + 22, 1);
  wr_le32(file + 24, 8000);
  wr_le32(file + 28, 8000);
  wr_le16(file + 32, 1);
  wr_le16(file + 34, 8);
  memcpy(file + 36, "data", 4);
  wr_le32(file + 40, 1);
  file[44] = 0xff; /* mu-law silence */
  assert(axLoadWAVMem(file, 45, &spec, &buf, &len) == TRUE);
  assert(spec.format == AX_AUDIO_S16);
  assert(spec.freq == 8000);
  assert(len == 2);
  memcpy(&sample, buf, 2);
  assert(sample == 0);
  axFreeWAV(buf);

  assert(axLoadWAVMem(bad, sizeof(bad), &spec, &buf, &len) == FALSE);
  assert(axAudioGetError() != NULL);
}

int main(void)
{
  test_pcm_and_mix();
  test_mulaw_and_reject();
  /* Opening a device is best-effort. Headless CI has no sound card. */
  {
    AXaudiospec want;
    int dev;
    memset(&want, 0, sizeof(want));
    want.freq = 22050;
    want.format = AX_AUDIO_S16;
    want.channels = 1;
    want.samples = 512;
    dev = axAudioOpen(&want, NULL);
    if (dev) {
      uint8_t silence[64];
      memset(silence, 0, sizeof(silence));
      assert(axAudioQueue(dev, silence, sizeof(silence)) == TRUE);
      assert(axAudioQueued(dev) == sizeof(silence));
      axAudioClearQueue(dev);
      assert(axAudioQueued(dev) == 0);
      axAudioClose(dev);
    }
  }
  axAudioShutdown();
  printf("Audio WAV tests passed.\n");
  return 0;
}
