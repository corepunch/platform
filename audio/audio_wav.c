/**
 * @file audio_wav.c
 * @brief WAV loader and SDL-style playback device.
 *
 * Loads RIFF WAVE PCM (8/16/24/32-bit), IEEE float 32, A-law, and mu-law.
 * Playback is queued or callback-driven, then converted to S16 for the
 * platform backend. Microsoft and IMA ADPCM are intentionally unsupported.
 */
#include "audio_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AX_AUDIO_QUEUE_MAX (4u * 1024u * 1024u)
#define AX_AUDIO_DEV_ID 1

static char g_audio_error[256];
static int g_audio_ready = 0;

typedef struct {
  int open;
  int paused;
  int hw_open;
  AXaudiospec spec;
  axmutex_t lock;
  uint8_t *queue;
  uint32_t cap;
  uint32_t len;
  uint32_t rd;
} ax_audio_dev;

static ax_audio_dev g_dev;

static void audio_set_error(char const *msg)
{
  if (!msg) msg = "unknown audio error";
  snprintf(g_audio_error, sizeof(g_audio_error), "%s", msg);
}

static uint16_t rd_le16(uint8_t const *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd_le32(uint8_t const *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int format_bytes(AXaudioformat format)
{
  switch (format) {
  case AX_AUDIO_U8:  return 1;
  case AX_AUDIO_S16: return 2;
  case AX_AUDIO_S32: return 4;
  case AX_AUDIO_F32: return 4;
  default:           return 0;
  }
}

static int valid_spec(AXaudiospec const *spec)
{
  if (!spec) return 0;
  if (spec->freq < 8000 || spec->freq > 192000) return 0;
  if (spec->channels < 1 || spec->channels > 2) return 0;
  return format_bytes(spec->format) != 0;
}

/* ITU-T G.711. Tables keep the decoder free of float. */
static int16_t mulaw_decode(uint8_t sample)
{
  int sign, exponent, mantissa, value;
  sample = (uint8_t)~sample;
  sign = sample & 0x80;
  exponent = (sample >> 4) & 0x07;
  mantissa = sample & 0x0f;
  value = ((mantissa << 3) + 0x84) << exponent;
  value -= 0x84;
  return (int16_t)(sign ? -value : value);
}

static int16_t alaw_decode(uint8_t sample)
{
  int sign, exponent, mantissa, value;
  sample ^= 0x55;
  sign = sample & 0x80;
  exponent = (sample >> 4) & 0x07;
  mantissa = sample & 0x0f;
  if (exponent == 0)
    value = (mantissa << 4) + 8;
  else
    value = ((mantissa << 4) + 0x108) << (exponent - 1);
  return (int16_t)(sign ? -value : value);
}

static bool_t decode_wav(uint8_t const *file, uint32_t size,
                         AXaudiospec *spec, uint8_t **audio_buf, uint32_t *audio_len)
{
  uint32_t off = 12;
  int got_fmt = 0;
  uint16_t tag = 0, channels = 0, bits = 0, block_align = 0;
  uint32_t rate = 0;
  uint8_t const *data = NULL;
  uint32_t data_len = 0;
  uint32_t frames, out_len, i, s;
  int out_bps;
  AXaudioformat format;
  uint8_t *out;

  if (!file || size < 12 || !spec || !audio_buf || !audio_len) {
    audio_set_error("invalid WAV arguments");
    return FALSE;
  }
  if (memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
    audio_set_error("not a RIFF WAVE file");
    return FALSE;
  }

  while (off + 8 <= size) {
    uint32_t chunk = rd_le32(file + off + 4);
    uint8_t const *body = file + off + 8;
    uint32_t avail = size - (off + 8);
    if (chunk > avail) chunk = avail; /* SDL-style lenient truncation */
    if (memcmp(file + off, "fmt ", 4) == 0 && chunk >= 16) {
      tag = rd_le16(body);
      channels = rd_le16(body + 2);
      rate = rd_le32(body + 4);
      block_align = rd_le16(body + 12);
      bits = rd_le16(body + 14);
      if (tag == 0xFFFE && chunk >= 40)
        tag = rd_le16(body + 24); /* WAVEFORMATEXTENSIBLE subformat tag */
      got_fmt = 1;
    } else if (memcmp(file + off, "data", 4) == 0 && !data) {
      data = body;
      data_len = chunk;
    }
    off += 8 + chunk + (chunk & 1);
  }

  if (!got_fmt || !data || channels < 1 || channels > 2 || rate < 8000 || rate > 192000) {
    audio_set_error("WAV is missing fmt/data or has an unsupported layout");
    return FALSE;
  }

  if (tag == 1 && bits == 8) format = AX_AUDIO_U8;
  else if (tag == 1 && bits == 16) format = AX_AUDIO_S16;
  else if (tag == 1 && (bits == 24 || bits == 32)) format = AX_AUDIO_S32;
  else if (tag == 3 && bits == 32) format = AX_AUDIO_F32;
  else if (tag == 6 || tag == 7) format = AX_AUDIO_S16; /* A-law / mu-law */
  else {
    audio_set_error("unsupported WAV encoding (PCM, float, A-law, mu-law only)");
    return FALSE;
  }

  if (tag == 1 || tag == 3) {
    uint32_t expect = (uint32_t)channels * (bits / 8);
    if (block_align && block_align != expect) {
      audio_set_error("WAV block align does not match bit depth");
      return FALSE;
    }
    block_align = (uint16_t)expect;
  } else if (block_align == 0) {
    block_align = channels;
  }

  frames = data_len / block_align;
  if (frames == 0) {
    audio_set_error("WAV data chunk is empty");
    return FALSE;
  }
  out_bps = format_bytes(format);
  out_len = frames * channels * (uint32_t)out_bps;
  out = (uint8_t *)malloc(out_len ? out_len : 1);
  if (!out) {
    audio_set_error("out of memory loading WAV");
    return FALSE;
  }

  for (i = 0; i < frames; i++) {
    uint8_t const *frame = data + i * block_align;
    for (s = 0; s < channels; s++) {
      uint8_t const *p = frame + s * (tag == 6 || tag == 7 ? 1 : (bits / 8));
      uint32_t o = (i * channels + s) * (uint32_t)out_bps;
      if (tag == 6) {
        int16_t v = alaw_decode(*p);
        memcpy(out + o, &v, 2);
      } else if (tag == 7) {
        int16_t v = mulaw_decode(*p);
        memcpy(out + o, &v, 2);
      } else if (bits == 8) {
        out[o] = *p;
      } else if (bits == 16) {
        int16_t v = (int16_t)rd_le16(p);
        memcpy(out + o, &v, 2);
      } else if (bits == 24) {
        int32_t v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
        if (v & 0x800000) v |= ~0xFFFFFF;
        v <<= 8;
        memcpy(out + o, &v, 4);
      } else {
        memcpy(out + o, p, 4); /* 32-bit PCM or IEEE float, little-endian host */
      }
    }
  }

  memset(spec, 0, sizeof(*spec));
  spec->freq = (int)rate;
  spec->format = format;
  spec->channels = (uint8_t)channels;
  spec->samples = 1024;
  *audio_buf = out;
  *audio_len = out_len;
  return TRUE;
}

bool_t
axAudioInit(void)
{
  if (g_audio_ready) return TRUE;
  g_dev.lock = axMutexCreate();
  if (!g_dev.lock) {
    audio_set_error("failed to create audio mutex");
    return FALSE;
  }
  g_audio_ready = 1;
  g_audio_error[0] = '\0';
  return TRUE;
}

void
axAudioShutdown(void)
{
  if (!g_audio_ready) return;
  axAudioClose(AX_AUDIO_DEV_ID);
  axMutexDestroy(g_dev.lock);
  g_dev.lock = NULL;
  g_audio_ready = 0;
}

char const *
axAudioGetError(void)
{
  return g_audio_error[0] ? g_audio_error : NULL;
}

int
axAudioOpen(AXaudiospec const *desired, AXaudiospec *obtained)
{
  AXaudiospec spec;
  int frames;

  if (!axAudioInit()) return 0;
  axMutexLock(g_dev.lock);
  if (g_dev.open) {
    axMutexUnlock(g_dev.lock);
    audio_set_error("audio device already open");
    return 0;
  }
  memset(&spec, 0, sizeof(spec));
  spec.freq = 44100;
  spec.format = AX_AUDIO_S16;
  spec.channels = 2;
  spec.samples = 1024;
  if (desired) {
    spec.freq = desired->freq ? desired->freq : spec.freq;
    spec.format = desired->format ? desired->format : spec.format;
    spec.channels = desired->channels ? desired->channels : spec.channels;
    spec.samples = desired->samples ? desired->samples : spec.samples;
    spec.callback = desired->callback;
    spec.userdata = desired->userdata;
  }
  if (!valid_spec(&spec)) {
    axMutexUnlock(g_dev.lock);
    audio_set_error("unsupported audio spec");
    return 0;
  }
  frames = spec.samples > 0 ? spec.samples : 1024;
  g_dev.spec = spec;
  g_dev.paused = 1;
  g_dev.len = 0;
  g_dev.rd = 0;
  g_dev.cap = 0;
  g_dev.queue = NULL;
  g_dev.open = 1;
  axMutexUnlock(g_dev.lock);

  if (!ax_audio_hw_open(spec.freq, spec.channels, frames)) {
    axMutexLock(g_dev.lock);
    g_dev.open = 0;
    axMutexUnlock(g_dev.lock);
    audio_set_error("failed to open audio device");
    return 0;
  }
  g_dev.hw_open = 1;
  ax_audio_hw_pause(TRUE);
  if (obtained) {
    *obtained = spec;
    obtained->callback = NULL;
    obtained->userdata = NULL;
  }
  g_audio_error[0] = '\0';
  return AX_AUDIO_DEV_ID;
}

void
axAudioClose(int dev)
{
  uint8_t *queue = NULL;
  if (!g_audio_ready || dev != AX_AUDIO_DEV_ID) return;
  if (g_dev.hw_open) {
    ax_audio_hw_close();
    g_dev.hw_open = 0;
  }
  axMutexLock(g_dev.lock);
  queue = g_dev.queue;
  g_dev.queue = NULL;
  g_dev.cap = g_dev.len = g_dev.rd = 0;
  g_dev.open = 0;
  g_dev.paused = 1;
  axMutexUnlock(g_dev.lock);
  free(queue);
}

void
axAudioPause(int dev, bool_t pause)
{
  if (!g_audio_ready || dev != AX_AUDIO_DEV_ID) return;
  axMutexLock(g_dev.lock);
  if (g_dev.open) g_dev.paused = pause ? 1 : 0;
  axMutexUnlock(g_dev.lock);
  if (g_dev.hw_open) ax_audio_hw_pause(pause ? TRUE : FALSE);
}

void
axAudioLock(int dev)
{
  if (g_audio_ready && dev == AX_AUDIO_DEV_ID && g_dev.lock)
    axMutexLock(g_dev.lock);
}

void
axAudioUnlock(int dev)
{
  if (g_audio_ready && dev == AX_AUDIO_DEV_ID && g_dev.lock)
    axMutexUnlock(g_dev.lock);
}

bool_t
axAudioQueue(int dev, void const *data, uint32_t len)
{
  uint32_t space, first;
  if (!data && len) return FALSE;
  if (!g_audio_ready || dev != AX_AUDIO_DEV_ID) return FALSE;
  axMutexLock(g_dev.lock);
  if (!g_dev.open || g_dev.spec.callback) {
    axMutexUnlock(g_dev.lock);
    audio_set_error("queue requires an open callback-less device");
    return FALSE;
  }
  if (g_dev.len + len > AX_AUDIO_QUEUE_MAX) {
    axMutexUnlock(g_dev.lock);
    audio_set_error("audio queue is full");
    return FALSE;
  }
  if (g_dev.cap < g_dev.len + len) {
    uint32_t cap = g_dev.cap ? g_dev.cap : 4096;
    uint8_t *grown;
    while (cap < g_dev.len + len) {
      if (cap > AX_AUDIO_QUEUE_MAX / 2) { cap = AX_AUDIO_QUEUE_MAX; break; }
      cap *= 2;
    }
    grown = (uint8_t *)realloc(g_dev.queue, cap);
    if (!grown) {
      axMutexUnlock(g_dev.lock);
      audio_set_error("out of memory queueing audio");
      return FALSE;
    }
    if (g_dev.len && g_dev.rd) {
      /* Compact the live bytes to the front after growth. */
      memmove(grown, grown + g_dev.rd, g_dev.len);
      g_dev.rd = 0;
    }
    g_dev.queue = grown;
    g_dev.cap = cap;
  }
  if (g_dev.rd + g_dev.len > g_dev.cap) {
    memmove(g_dev.queue, g_dev.queue + g_dev.rd, g_dev.len);
    g_dev.rd = 0;
  }
  space = g_dev.cap - (g_dev.rd + g_dev.len);
  first = len < space ? len : space;
  memcpy(g_dev.queue + g_dev.rd + g_dev.len, data, first);
  if (first < len)
    memcpy(g_dev.queue, (uint8_t const *)data + first, len - first);
  g_dev.len += len;
  axMutexUnlock(g_dev.lock);
  return TRUE;
}

uint32_t
axAudioQueued(int dev)
{
  uint32_t n = 0;
  if (!g_audio_ready || dev != AX_AUDIO_DEV_ID) return 0;
  axMutexLock(g_dev.lock);
  n = g_dev.len;
  axMutexUnlock(g_dev.lock);
  return n;
}

void
axAudioClearQueue(int dev)
{
  if (!g_audio_ready || dev != AX_AUDIO_DEV_ID) return;
  axMutexLock(g_dev.lock);
  g_dev.len = 0;
  g_dev.rd = 0;
  axMutexUnlock(g_dev.lock);
}

static void take_user_bytes(uint8_t *dst, int nbytes)
{
  int got = 0;
  if (g_dev.spec.callback) {
    axMutexUnlock(g_dev.lock);
    g_dev.spec.callback(g_dev.spec.userdata, dst, nbytes);
    axMutexLock(g_dev.lock);
    return;
  }
  if (g_dev.paused || g_dev.len == 0) {
    memset(dst, 0, (size_t)nbytes);
    return;
  }
  if ((uint32_t)nbytes > g_dev.len) got = (int)g_dev.len;
  else got = nbytes;
  if (g_dev.rd + g_dev.len > g_dev.cap) {
    memmove(g_dev.queue, g_dev.queue + g_dev.rd, g_dev.len);
    g_dev.rd = 0;
  }
  memcpy(dst, g_dev.queue + g_dev.rd, (size_t)got);
  g_dev.rd += (uint32_t)got;
  g_dev.len -= (uint32_t)got;
  if (g_dev.len == 0) g_dev.rd = 0;
  if (got < nbytes) memset(dst + got, 0, (size_t)(nbytes - got));
}

static int16_t sample_to_s16(uint8_t const *p, AXaudioformat format)
{
  switch (format) {
  case AX_AUDIO_U8:
    return (int16_t)(((int)*p - 128) << 8);
  case AX_AUDIO_S16: {
    int16_t v;
    memcpy(&v, p, 2);
    return v;
  }
  case AX_AUDIO_S32: {
    int32_t v;
    memcpy(&v, p, 4);
    return (int16_t)(v >> 16);
  }
  case AX_AUDIO_F32: {
    float f;
    int s;
    memcpy(&f, p, 4);
    if (f > 1.f) f = 1.f;
    if (f < -1.f) f = -1.f;
    s = (int)(f * 32767.f);
    return (int16_t)s;
  }
  default:
    return 0;
  }
}

void
ax_audio_device_fill(void *dst, int nbytes)
{
  int16_t *out = (int16_t *)dst;
  int channels, bps, frames, i, c;
  uint8_t *user = NULL;
  int user_bytes;

  if (!dst || nbytes <= 0) return;
  if (!g_audio_ready || !g_dev.open) {
    memset(dst, 0, (size_t)nbytes);
    return;
  }
  axMutexLock(g_dev.lock);
  channels = g_dev.spec.channels;
  bps = format_bytes(g_dev.spec.format);
  frames = nbytes / (channels * 2);
  user_bytes = frames * channels * bps;
  if (frames <= 0 || bps <= 0) {
    memset(dst, 0, (size_t)nbytes);
    axMutexUnlock(g_dev.lock);
    return;
  }
  if (g_dev.paused) {
    memset(dst, 0, (size_t)nbytes);
    axMutexUnlock(g_dev.lock);
    return;
  }
  user = (uint8_t *)malloc((size_t)user_bytes);
  if (!user) {
    memset(dst, 0, (size_t)nbytes);
    axMutexUnlock(g_dev.lock);
    return;
  }
  take_user_bytes(user, user_bytes);
  for (i = 0; i < frames; i++) {
    for (c = 0; c < channels; c++) {
      out[i * channels + c] = sample_to_s16(user + (i * channels + c) * bps, g_dev.spec.format);
    }
  }
  free(user);
  axMutexUnlock(g_dev.lock);
}

bool_t
axLoadWAV(char const *path, AXaudiospec *spec, uint8_t **audio_buf, uint32_t *audio_len)
{
  FILE *f;
  long sz;
  uint8_t *file = NULL;
  bool_t ok;
  if (!path || !spec || !audio_buf || !audio_len) {
    audio_set_error("invalid WAV arguments");
    return FALSE;
  }
  f = fopen(path, "rb");
  if (!f) {
    audio_set_error("failed to open WAV file");
    return FALSE;
  }
  if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 12 || sz > 64 * 1024 * 1024) {
    fclose(f);
    audio_set_error("WAV file is missing or too large");
    return FALSE;
  }
  rewind(f);
  file = (uint8_t *)malloc((size_t)sz);
  if (!file || fread(file, 1, (size_t)sz, f) != (size_t)sz) {
    free(file);
    fclose(f);
    audio_set_error("failed to read WAV file");
    return FALSE;
  }
  fclose(f);
  ok = decode_wav(file, (uint32_t)sz, spec, audio_buf, audio_len);
  free(file);
  return ok;
}

bool_t
axLoadWAVMem(void const *data, uint32_t size, AXaudiospec *spec,
             uint8_t **audio_buf, uint32_t *audio_len)
{
  return decode_wav((uint8_t const *)data, size, spec, audio_buf, audio_len);
}

void
axFreeWAV(uint8_t *audio_buf)
{
  free(audio_buf);
}

static int clamp_s16(int v)
{
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return v;
}

void
axAudioMix(uint8_t *dst, uint8_t const *src, AXaudioformat format, uint32_t len, int volume)
{
  uint32_t i;
  if (!dst || !src || len == 0) return;
  if (volume < 0) volume = 0;
  if (volume > 128) volume = 128;
  switch (format) {
  case AX_AUDIO_U8:
    for (i = 0; i < len; i++) {
      int mixed = ((int)dst[i] - 128) + ((((int)src[i] - 128) * volume) / 128);
      dst[i] = (uint8_t)(clamp_s16(mixed) + 128);
    }
    break;
  case AX_AUDIO_S16:
    for (i = 0; i + 1 < len; i += 2) {
      int16_t d, s;
      memcpy(&d, dst + i, 2);
      memcpy(&s, src + i, 2);
      d = (int16_t)clamp_s16(d + (s * volume) / 128);
      memcpy(dst + i, &d, 2);
    }
    break;
  case AX_AUDIO_S32:
    for (i = 0; i + 3 < len; i += 4) {
      int32_t d, s;
      int64_t mixed;
      memcpy(&d, dst + i, 4);
      memcpy(&s, src + i, 4);
      mixed = (int64_t)d + ((int64_t)s * volume) / 128;
      if (mixed > 2147483647LL) mixed = 2147483647LL;
      if (mixed < -2147483648LL) mixed = -2147483648LL;
      d = (int32_t)mixed;
      memcpy(dst + i, &d, 4);
    }
    break;
  case AX_AUDIO_F32:
    for (i = 0; i + 3 < len; i += 4) {
      float d, s;
      memcpy(&d, dst + i, 4);
      memcpy(&s, src + i, 4);
      d += s * ((float)volume / 128.f);
      if (d > 1.f) d = 1.f;
      if (d < -1.f) d = -1.f;
      memcpy(dst + i, &d, 4);
    }
    break;
  default:
    break;
  }
}
