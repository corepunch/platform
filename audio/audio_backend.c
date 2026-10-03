/**
 * @file audio_backend.c
 * @brief Platform playback for the shared S16 device.
 *
 * macOS and iOS use AudioQueue, Windows uses waveOut, Linux uses ALSA when
 * the headers were available at build time. Other targets compile a stub
 * that reports no device so WAV loading and mixing still work.
 */
#include "audio_backend.h"

#include <string.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioQueue.h>
#include <stdlib.h>

#define AX_HW_BUFFERS 3

static AudioQueueRef g_queue = NULL;
static AudioQueueBufferRef g_bufs[AX_HW_BUFFERS];
static int g_hw_paused = 1;

static void apple_fill(void *userdata, AudioQueueRef queue, AudioQueueBufferRef buf)
{
  (void)userdata;
  ax_audio_device_fill(buf->mAudioData, (int)buf->mAudioDataByteSize);
  AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
}

bool_t
ax_audio_hw_open(int freq, int channels, int frames)
{
  AudioStreamBasicDescription fmt;
  int i;
  UInt32 bytes;
  if (g_queue) return TRUE;
  if (frames < 256) frames = 256;
  memset(&fmt, 0, sizeof(fmt));
  fmt.mSampleRate = freq;
  fmt.mFormatID = kAudioFormatLinearPCM;
  fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
  fmt.mChannelsPerFrame = (UInt32)channels;
  fmt.mBitsPerChannel = 16;
  fmt.mFramesPerPacket = 1;
  fmt.mBytesPerFrame = (UInt32)(channels * 2);
  fmt.mBytesPerPacket = fmt.mBytesPerFrame;
  if (AudioQueueNewOutput(&fmt, apple_fill, NULL, NULL, NULL, 0, &g_queue) != noErr)
    return FALSE;
  bytes = (UInt32)(frames * channels * 2);
  for (i = 0; i < AX_HW_BUFFERS; i++) {
    if (AudioQueueAllocateBuffer(g_queue, bytes, &g_bufs[i]) != noErr) {
      ax_audio_hw_close();
      return FALSE;
    }
    g_bufs[i]->mAudioDataByteSize = bytes;
    memset(g_bufs[i]->mAudioData, 0, bytes);
    AudioQueueEnqueueBuffer(g_queue, g_bufs[i], 0, NULL);
  }
  g_hw_paused = 1;
  return TRUE;
}

void
ax_audio_hw_close(void)
{
  int i;
  if (!g_queue) return;
  AudioQueueStop(g_queue, true);
  for (i = 0; i < AX_HW_BUFFERS; i++) g_bufs[i] = NULL;
  AudioQueueDispose(g_queue, true);
  g_queue = NULL;
  g_hw_paused = 1;
}

void
ax_audio_hw_pause(bool_t pause)
{
  if (!g_queue) return;
  if (pause) {
    AudioQueuePause(g_queue);
    g_hw_paused = 1;
  } else if (g_hw_paused) {
    AudioQueueStart(g_queue, NULL);
    g_hw_paused = 0;
  }
}

#elif defined(_WIN32) || defined(__MINGW32__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#define AX_HW_BUFFERS 3

static HWAVEOUT g_wave = NULL;
static WAVEHDR g_hdrs[AX_HW_BUFFERS];
static int g_hw_paused = 1;

static void CALLBACK wave_done(HWAVEOUT wave, UINT msg, DWORD_PTR user, DWORD_PTR p1, DWORD_PTR p2)
{
  WAVEHDR *hdr = (WAVEHDR *)p1;
  (void)user;
  (void)p2;
  if (msg != WOM_DONE || !hdr) return;
  ax_audio_device_fill(hdr->lpData, (int)hdr->dwBufferLength);
  waveOutWrite(wave, hdr, sizeof(*hdr));
}

bool_t
ax_audio_hw_open(int freq, int channels, int frames)
{
  WAVEFORMATEX wfx;
  int i;
  MMRESULT rc;
  if (g_wave) return TRUE;
  if (frames < 256) frames = 256;
  memset(&wfx, 0, sizeof(wfx));
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nChannels = (WORD)channels;
  wfx.nSamplesPerSec = (DWORD)freq;
  wfx.wBitsPerSample = 16;
  wfx.nBlockAlign = (WORD)(channels * 2);
  wfx.nAvgBytesPerSec = (DWORD)(freq * wfx.nBlockAlign);
  rc = waveOutOpen(&g_wave, WAVE_MAPPER, &wfx, (DWORD_PTR)wave_done, 0, CALLBACK_FUNCTION);
  if (rc != MMSYSERR_NOERROR) {
    g_wave = NULL;
    return FALSE;
  }
  for (i = 0; i < AX_HW_BUFFERS; i++) {
    memset(&g_hdrs[i], 0, sizeof(g_hdrs[i]));
    g_hdrs[i].dwBufferLength = (DWORD)(frames * channels * 2);
    g_hdrs[i].lpData = (LPSTR)malloc(g_hdrs[i].dwBufferLength);
    if (!g_hdrs[i].lpData) {
      ax_audio_hw_close();
      return FALSE;
    }
    memset(g_hdrs[i].lpData, 0, g_hdrs[i].dwBufferLength);
    waveOutPrepareHeader(g_wave, &g_hdrs[i], sizeof(g_hdrs[i]));
    waveOutWrite(g_wave, &g_hdrs[i], sizeof(g_hdrs[i]));
  }
  waveOutPause(g_wave);
  g_hw_paused = 1;
  return TRUE;
}

void
ax_audio_hw_close(void)
{
  int i;
  if (g_wave) {
    waveOutReset(g_wave);
    for (i = 0; i < AX_HW_BUFFERS; i++) {
      if (g_hdrs[i].lpData) {
        waveOutUnprepareHeader(g_wave, &g_hdrs[i], sizeof(g_hdrs[i]));
        free(g_hdrs[i].lpData);
        g_hdrs[i].lpData = NULL;
      }
    }
    waveOutClose(g_wave);
    g_wave = NULL;
  } else {
    for (i = 0; i < AX_HW_BUFFERS; i++) {
      free(g_hdrs[i].lpData);
      g_hdrs[i].lpData = NULL;
    }
  }
  g_hw_paused = 1;
}

void
ax_audio_hw_pause(bool_t pause)
{
  if (!g_wave) return;
  if (pause) {
    waveOutPause(g_wave);
    g_hw_paused = 1;
  } else {
    waveOutRestart(g_wave);
    g_hw_paused = 0;
  }
}

#elif defined(__linux__) && defined(HAVE_ALSA)
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdlib.h>

static snd_pcm_t *g_pcm = NULL;
static pthread_t g_thread;
static int g_thread_started = 0;
static volatile int g_run = 0;
static int g_period_bytes = 0;
static int g_period_frames = 0;
static int16_t *g_buf = NULL;

static void *alsa_thread(void *arg)
{
  (void)arg;
  while (g_run) {
    snd_pcm_sframes_t wrote;
    ax_audio_device_fill(g_buf, g_period_bytes);
    wrote = snd_pcm_writei(g_pcm, g_buf, (snd_pcm_uframes_t)g_period_frames);
    if (wrote < 0)
      snd_pcm_recover(g_pcm, (int)wrote, 1);
  }
  return NULL;
}

bool_t
ax_audio_hw_open(int freq, int channels, int frames)
{
  snd_pcm_hw_params_t *params = NULL;
  int err, dir = 0;
  unsigned int rate = (unsigned int)freq;
  snd_pcm_uframes_t period = (snd_pcm_uframes_t)frames;
  if (g_pcm) return TRUE;
  if (frames < 256) frames = 256;
  err = snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) return FALSE;
  snd_pcm_hw_params_alloca(&params);
  snd_pcm_hw_params_any(g_pcm, params);
  snd_pcm_hw_params_set_access(g_pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
  snd_pcm_hw_params_set_format(g_pcm, params, SND_PCM_FORMAT_S16);
  snd_pcm_hw_params_set_channels(g_pcm, params, (unsigned int)channels);
  snd_pcm_hw_params_set_rate_near(g_pcm, params, &rate, &dir);
  snd_pcm_hw_params_set_period_size_near(g_pcm, params, &period, &dir);
  if (snd_pcm_hw_params(g_pcm, params) < 0) {
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
    return FALSE;
  }
  g_period_frames = (int)period;
  g_period_bytes = (int)(period * (snd_pcm_uframes_t)channels * 2);
  g_buf = (int16_t *)calloc(1, (size_t)g_period_bytes);
  if (!g_buf) {
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
    return FALSE;
  }
  g_run = 1;
  if (pthread_create(&g_thread, NULL, alsa_thread, NULL) != 0) {
    free(g_buf);
    g_buf = NULL;
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
    g_run = 0;
    return FALSE;
  }
  g_thread_started = 1;
  return TRUE;
}

void
ax_audio_hw_close(void)
{
  g_run = 0;
  if (g_pcm)
    snd_pcm_drop(g_pcm); /* unblock a thread stuck in snd_pcm_writei */
  if (g_thread_started) {
    pthread_join(g_thread, NULL);
    g_thread_started = 0;
  }
  free(g_buf);
  g_buf = NULL;
  if (g_pcm) {
    snd_pcm_close(g_pcm);
    g_pcm = NULL;
  }
}

void
ax_audio_hw_pause(bool_t pause)
{
  /* Paused devices are fed silence by ax_audio_device_fill. snd_pcm_pause
   * is not implemented by every ALSA plugin, so it is not required here. */
  (void)pause;
}

#else

bool_t
ax_audio_hw_open(int freq, int channels, int frames)
{
  (void)freq;
  (void)channels;
  (void)frames;
  return FALSE;
}

void
ax_audio_hw_close(void)
{
}

void
ax_audio_hw_pause(bool_t pause)
{
  (void)pause;
}

#endif
