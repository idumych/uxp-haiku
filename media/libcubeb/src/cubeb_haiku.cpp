/*
 * Copyright © 2026 Basilisk Haiku port contributors
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */

/* Output-only cubeb backend for Haiku, using the Media Kit's BSoundPlayer.
 * The system mixer converts sample rate and format, so the stream is opened
 * with exactly the parameters the caller asked for. Inspired by the Haiku
 * cubeb backend in HaikuPorts' Firefox patchset, written for the older
 * cubeb API in UXP. */

#include <media/SoundPlayer.h>
#include <media/MediaDefs.h>
#include <kernel/OS.h>
#include <support/Errors.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern "C" {
#include "cubeb/cubeb.h"
#include "cubeb-internal.h"
}

static struct cubeb_ops const haiku_ops;

struct cubeb {
  struct cubeb_ops const * ops;
};

struct cubeb_stream {
  cubeb * context;
  BSoundPlayer * player;
  cubeb_data_callback data_callback;
  cubeb_state_callback state_callback;
  void * user_ptr;
  cubeb_stream_params params;
  size_t frame_size;
  pthread_mutex_t mutex;
  uint64_t frames_written;  /* frames handed to the mixer */
  bigtime_t last_time;      /* system_time() at the last callback */
  bool draining;
  bool drained;
  bool errored;
};

extern "C" int haiku_init(cubeb ** context, char const * context_name);

int
haiku_init(cubeb ** context, char const * context_name)
{
  (void) context_name;
  cubeb * ctx = (cubeb *) calloc(1, sizeof(cubeb));
  if (!ctx) {
    return CUBEB_ERROR;
  }
  ctx->ops = &haiku_ops;
  *context = ctx;
  return CUBEB_OK;
}

static char const *
haiku_get_backend_id(cubeb * context)
{
  (void) context;
  return "haiku";
}

static void
haiku_destroy(cubeb * context)
{
  free(context);
}

static int
haiku_get_max_channel_count(cubeb * ctx, uint32_t * max_channels)
{
  (void) ctx;
  *max_channels = 2;
  return CUBEB_OK;
}

static int
haiku_get_preferred_sample_rate(cubeb * ctx, uint32_t * rate)
{
  (void) ctx;
  *rate = 48000;
  return CUBEB_OK;
}

static int
haiku_get_min_latency(cubeb * ctx, cubeb_stream_params params,
                      uint32_t * latency_ms)
{
  (void) ctx;
  (void) params;
  *latency_ms = 20;
  return CUBEB_OK;
}

static void
haiku_play_buffer(void * cookie, void * buffer, size_t size,
                  const media_raw_audio_format & format)
{
  (void) format;
  cubeb_stream * stm = (cubeb_stream *) cookie;
  long frames = size / stm->frame_size;
  long got = 0;
  bool notify_drained = false;

  if (!stm->draining && !stm->errored) {
    got = stm->data_callback(stm, stm->user_ptr, NULL, buffer, frames);
    if (got < 0) {
      stm->errored = true;
      got = 0;
      stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_ERROR);
    } else if (got < frames) {
      stm->draining = true;
    }
  }
  if (got < frames) {
    memset((char *) buffer + got * stm->frame_size, 0,
           (frames - got) * stm->frame_size);
  }

  pthread_mutex_lock(&stm->mutex);
  stm->frames_written += got;
  stm->last_time = system_time();
  if (stm->draining && !stm->drained && got < frames) {
    stm->drained = true;
    notify_drained = true;
  }
  pthread_mutex_unlock(&stm->mutex);

  if (notify_drained) {
    stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_DRAINED);
  }
}

static int
haiku_stream_init(cubeb * context, cubeb_stream ** stream,
                  char const * stream_name,
                  cubeb_devid input_device,
                  cubeb_stream_params * input_stream_params,
                  cubeb_devid output_device,
                  cubeb_stream_params * output_stream_params,
                  unsigned int latency,
                  cubeb_data_callback data_callback,
                  cubeb_state_callback state_callback,
                  void * user_ptr)
{
  (void) input_device;
  (void) output_device;

  if (input_stream_params || !output_stream_params) {
    return CUBEB_ERROR_NOT_SUPPORTED;
  }

  media_raw_audio_format fmt = media_raw_audio_format::wildcard;
  size_t sample_size;
  switch (output_stream_params->format) {
  case CUBEB_SAMPLE_S16LE:
    fmt.format = media_raw_audio_format::B_AUDIO_SHORT;
    fmt.byte_order = B_MEDIA_LITTLE_ENDIAN;
    sample_size = 2;
    break;
  case CUBEB_SAMPLE_S16BE:
    fmt.format = media_raw_audio_format::B_AUDIO_SHORT;
    fmt.byte_order = B_MEDIA_BIG_ENDIAN;
    sample_size = 2;
    break;
  case CUBEB_SAMPLE_FLOAT32LE:
    fmt.format = media_raw_audio_format::B_AUDIO_FLOAT;
    fmt.byte_order = B_MEDIA_LITTLE_ENDIAN;
    sample_size = 4;
    break;
  case CUBEB_SAMPLE_FLOAT32BE:
    fmt.format = media_raw_audio_format::B_AUDIO_FLOAT;
    fmt.byte_order = B_MEDIA_BIG_ENDIAN;
    sample_size = 4;
    break;
  default:
    return CUBEB_ERROR_INVALID_FORMAT;
  }

  cubeb_stream * stm = (cubeb_stream *) calloc(1, sizeof(cubeb_stream));
  if (!stm) {
    return CUBEB_ERROR;
  }
  stm->context = context;
  stm->data_callback = data_callback;
  stm->state_callback = state_callback;
  stm->user_ptr = user_ptr;
  stm->params = *output_stream_params;
  stm->frame_size = sample_size * output_stream_params->channels;
  pthread_mutex_init(&stm->mutex, NULL);

  /* latency is in frames; use it as a hint for the buffer size, but keep
   * buffers between ~5 ms and ~50 ms. */
  unsigned int rate = output_stream_params->rate;
  unsigned int buffer_frames = latency / 2;
  if (buffer_frames < rate / 200) {
    buffer_frames = rate / 200;
  }
  if (buffer_frames > rate / 20) {
    buffer_frames = rate / 20;
  }

  fmt.frame_rate = rate;
  fmt.channel_count = output_stream_params->channels;
  fmt.buffer_size = buffer_frames * stm->frame_size;

  stm->player = new BSoundPlayer(&fmt, stream_name ? stream_name : "cubeb",
                                 haiku_play_buffer, NULL, stm);
  if (!stm->player || stm->player->InitCheck() != B_OK) {
    delete stm->player;
    pthread_mutex_destroy(&stm->mutex);
    free(stm);
    return CUBEB_ERROR;
  }

  *stream = stm;
  return CUBEB_OK;
}

static void
haiku_stream_destroy(cubeb_stream * stm)
{
  if (!stm) {
    return;
  }
  if (stm->player) {
    stm->player->Stop(true, true);
    delete stm->player;
  }
  pthread_mutex_destroy(&stm->mutex);
  free(stm);
}

static int
haiku_stream_start(cubeb_stream * stm)
{
  stm->draining = false;
  stm->drained = false;
  stm->player->SetHasData(true);
  if (stm->player->Start() != B_OK) {
    return CUBEB_ERROR;
  }
  stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_STARTED);
  return CUBEB_OK;
}

static int
haiku_stream_stop(cubeb_stream * stm)
{
  stm->player->Stop(true, true);
  stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_STOPPED);
  return CUBEB_OK;
}

static uint64_t
haiku_latency_frames(cubeb_stream * stm)
{
  bigtime_t lat = stm->player->Latency();  /* microseconds */
  if (lat < 0) {
    lat = 0;
  }
  return (uint64_t) lat * stm->params.rate / 1000000;
}

static int
haiku_stream_get_position(cubeb_stream * stm, uint64_t * position)
{
  uint64_t lat = haiku_latency_frames(stm);
  pthread_mutex_lock(&stm->mutex);
  uint64_t written = stm->frames_written;
  pthread_mutex_unlock(&stm->mutex);
  *position = written > lat ? written - lat : 0;
  return CUBEB_OK;
}

static int
haiku_stream_get_latency(cubeb_stream * stm, uint32_t * latency)
{
  *latency = (uint32_t) haiku_latency_frames(stm);
  return CUBEB_OK;
}

static int
haiku_stream_set_volume(cubeb_stream * stm, float volume)
{
  stm->player->SetVolume(volume);
  return CUBEB_OK;
}

static struct cubeb_ops const haiku_ops = {
  /*.init =*/ haiku_init,
  /*.get_backend_id =*/ haiku_get_backend_id,
  /*.get_max_channel_count =*/ haiku_get_max_channel_count,
  /*.get_min_latency =*/ haiku_get_min_latency,
  /*.get_preferred_sample_rate =*/ haiku_get_preferred_sample_rate,
  /*.enumerate_devices =*/ NULL,
  /*.destroy =*/ haiku_destroy,
  /*.stream_init =*/ haiku_stream_init,
  /*.stream_destroy =*/ haiku_stream_destroy,
  /*.stream_start =*/ haiku_stream_start,
  /*.stream_stop =*/ haiku_stream_stop,
  /*.stream_get_position =*/ haiku_stream_get_position,
  /*.stream_get_latency =*/ haiku_stream_get_latency,
  /*.stream_set_volume =*/ haiku_stream_set_volume,
  /*.stream_set_panning =*/ NULL,
  /*.stream_get_current_device =*/ NULL,
  /*.stream_device_destroy =*/ NULL,
  /*.stream_register_device_changed_callback =*/ NULL,
  /*.register_device_collection_changed =*/ NULL
};
