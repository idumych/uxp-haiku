/*
 * Copyright © 2026 Basilisk Haiku port contributors
 *
 * This program is made available under an ISC-style license.  See the
 * accompanying file LICENSE for details.
 */

/* cubeb backend for Haiku, written for the older cubeb API in UXP.
 *
 * Output uses the Media Kit's BSoundPlayer, opened with exactly the
 * parameters the caller asked for: the system mixer converts rate and format.
 *
 * Input uses BMediaRecorder connected to the default audio input. The
 * recorder delivers whatever the input node produces, so buffers are
 * converted here to the stream's sample format and channel count and
 * linearly resampled to the stream rate. Input-only streams call the data
 * callback from the recorder thread; duplex streams queue converted input in
 * a FIFO (in input-format frames, which may have fewer channels than the
 * output) that the output (BSoundPlayer) callback drains.
 *
 * Device enumeration lists the physical input/output nodes known to
 * BMediaRoster. Streams always use the system default devices.
 *
 * Inspired by the Haiku cubeb backend in HaikuPorts' Firefox patchset. */

#include <media/MediaDefs.h>
#include <media/MediaNode.h>
#include <media/MediaRecorder.h>
#include <media/MediaRoster.h>
#include <media/SoundPlayer.h>
#include <kernel/OS.h>
#include <support/Errors.h>

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <vector>

extern "C" {
#include "cubeb/cubeb.h"
#include "cubeb-internal.h"
}

struct cubeb {
  struct cubeb_ops const * ops;
};

/* Converts recorder buffers (any raw audio format, any rate/channels) to
 * the stream format, channel count and rate. */
struct haiku_input_converter {
  unsigned int out_rate;
  unsigned int out_channels;
  bool out_float;
  /* Resampler state: position of the next output frame, in input frames,
   * relative to the first frame of the current input buffer (can be
   * negative: -1 means "between the previous buffer's last frame and this
   * buffer's first frame"). */
  double pos;
  std::vector<float> last;  /* last input frame, out_channels samples */
  bool have_last;
  std::vector<float> scratch;  /* input converted to float, out_channels */

  void init(unsigned int rate, unsigned int channels, bool is_float)
  {
    out_rate = rate;
    out_channels = channels;
    out_float = is_float;
    pos = 0;
    last.assign(channels, 0.f);
    have_last = false;
  }

  /* Read one input sample as float in [-1, 1]. */
  static float sample(const void * data, size_t i, uint32 fmt)
  {
    switch (fmt) {
    case media_raw_audio_format::B_AUDIO_FLOAT:
      return ((const float *) data)[i];
    case media_raw_audio_format::B_AUDIO_INT:
      return ((const int32 *) data)[i] / 2147483648.f;
    case media_raw_audio_format::B_AUDIO_SHORT:
      return ((const int16 *) data)[i] / 32768.f;
    case media_raw_audio_format::B_AUDIO_CHAR:
      return ((const int8 *) data)[i] / 128.f;
    case media_raw_audio_format::B_AUDIO_UCHAR:
      return (((const uint8 *) data)[i] - 128) / 128.f;
    }
    return 0.f;
  }

  static size_t sample_size(uint32 fmt)
  {
    return fmt & media_raw_audio_format::B_AUDIO_SIZE_MASK;
  }

  /* Append converted frames to out (in the stream's sample format). */
  void convert(const void * data, size_t size,
               const media_raw_audio_format & in, std::vector<char> & out)
  {
    unsigned int in_ch = in.channel_count ? in.channel_count : 1;
    size_t ss = sample_size(in.format);
    if (!ss || in.frame_rate <= 0) {
      return;
    }
    size_t frames = size / (ss * in_ch);
    if (!frames) {
      return;
    }

    /* Channel conversion into scratch: downmix to mono by averaging,
     * otherwise map channel c to min(c, in_ch - 1). */
    scratch.resize(frames * out_channels);
    for (size_t f = 0; f < frames; f++) {
      if (out_channels == 1 && in_ch > 1) {
        float sum = 0;
        for (unsigned int c = 0; c < in_ch; c++) {
          sum += sample(data, f * in_ch + c, in.format);
        }
        scratch[f] = sum / in_ch;
      } else {
        for (unsigned int c = 0; c < out_channels; c++) {
          unsigned int ic = c < in_ch ? c : in_ch - 1;
          scratch[f * out_channels + c] = sample(data, f * in_ch + ic, in.format);
        }
      }
    }

    /* Linear resampling to out_rate. */
    double step = in.frame_rate / (double) out_rate;
    if (!have_last) {
      for (unsigned int c = 0; c < out_channels; c++) {
        last[c] = scratch[c];
      }
      have_last = true;
      pos = 0;
    }
    size_t out_sample = out_float ? sizeof(float) : sizeof(int16_t);
    while (pos < (double) (frames - 1) || (pos < 0)) {
      long i0 = (long) (pos < 0 ? -1 : pos);
      double frac = pos - i0;
      size_t base = out.size();
      out.resize(base + out_channels * out_sample);
      for (unsigned int c = 0; c < out_channels; c++) {
        float a = i0 < 0 ? last[c] : scratch[i0 * out_channels + c];
        float b = scratch[(i0 + 1) * out_channels + c];
        float v = a + (b - a) * (float) frac;
        if (out_float) {
          ((float *) &out[base])[c] = v;
        } else {
          float s = v * 32768.f;
          s = s > 32767.f ? 32767.f : (s < -32768.f ? -32768.f : s);
          ((int16_t *) &out[base])[c] = (int16_t) s;
        }
      }
      pos += step;
    }
    pos -= frames;
    for (unsigned int c = 0; c < out_channels; c++) {
      last[c] = scratch[(frames - 1) * out_channels + c];
    }
  }
};

struct cubeb_stream {
  cubeb * context;
  BSoundPlayer * player;
  BMediaRecorder * recorder;
  cubeb_data_callback data_callback;
  cubeb_state_callback state_callback;
  void * user_ptr;
  cubeb_stream_params params;   /* output params, or input params */
  bool has_output;
  bool has_input;
  size_t frame_size;            /* bytes per stream frame */
  size_t in_frame_size;         /* bytes per input frame (input params) */
  pthread_mutex_t mutex;
  uint64_t frames_written;      /* output frames handed to the mixer */
  uint64_t frames_read;         /* input frames delivered (input-only) */
  bool draining;
  bool drained;
  bool errored;
  bool running;
  haiku_input_converter conv;
  std::vector<char> in_converted;   /* recorder thread scratch */
  std::vector<char> in_fifo;        /* duplex: converted input, guarded */
  std::vector<char> in_for_output;  /* output thread scratch */
};

extern "C" int haiku_init(cubeb ** context, char const * context_name);


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
    const void * input = NULL;
    if (stm->has_input) {
      /* Take up to `frames` frames of queued input, pad with silence. */
      size_t want = frames * stm->in_frame_size;
      stm->in_for_output.assign(want, 0);
      pthread_mutex_lock(&stm->mutex);
      size_t avail = stm->in_fifo.size();
      size_t take = avail < want ? avail : want;
      if (take) {
        memcpy(&stm->in_for_output[0], &stm->in_fifo[0], take);
        stm->in_fifo.erase(stm->in_fifo.begin(), stm->in_fifo.begin() + take);
      }
      pthread_mutex_unlock(&stm->mutex);
      input = &stm->in_for_output[0];
    }
    got = stm->data_callback(stm, stm->user_ptr, input, buffer, frames);
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
  if (stm->draining && !stm->drained && got < frames) {
    stm->drained = true;
    notify_drained = true;
  }
  pthread_mutex_unlock(&stm->mutex);

  if (notify_drained) {
    stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_DRAINED);
  }
}

static void
haiku_record_buffer(void * cookie, bigtime_t timestamp, void * data,
                    size_t size, const media_format & format)
{
  (void) timestamp;
  cubeb_stream * stm = (cubeb_stream *) cookie;
  if (!stm->running || stm->errored || stm->draining ||
      format.type != B_MEDIA_RAW_AUDIO) {
    return;
  }

  stm->in_converted.clear();
  stm->conv.convert(data, size, format.u.raw_audio, stm->in_converted);
  size_t bytes = stm->in_converted.size();
  if (!bytes) {
    return;
  }

  if (stm->has_output) {
    /* Duplex: queue for the output callback. Input and output clocks drift
     * apart, so cap the queue at ~100 ms to keep the latency low. */
    size_t cap = (stm->params.rate / 10) * stm->in_frame_size;
    pthread_mutex_lock(&stm->mutex);
    stm->in_fifo.insert(stm->in_fifo.end(), stm->in_converted.begin(),
                        stm->in_converted.end());
    if (stm->in_fifo.size() > cap) {
      size_t drop = stm->in_fifo.size() - cap;
      drop -= drop % stm->in_frame_size;
      stm->in_fifo.erase(stm->in_fifo.begin(), stm->in_fifo.begin() + drop);
    }
    pthread_mutex_unlock(&stm->mutex);
    return;
  }

  long frames = bytes / stm->in_frame_size;
  long got = stm->data_callback(stm, stm->user_ptr, &stm->in_converted[0],
                                NULL, frames);
  if (got < 0) {
    stm->errored = true;
    stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_ERROR);
    return;
  }
  pthread_mutex_lock(&stm->mutex);
  stm->frames_read += frames;
  pthread_mutex_unlock(&stm->mutex);
  if (got < frames) {
    stm->draining = true;
    stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_DRAINED);
  }
}

static bool
haiku_sample_format(cubeb_sample_format f, uint32 * fmt, uint32 * order,
                    size_t * sample_size)
{
  switch (f) {
  case CUBEB_SAMPLE_S16LE:
    *fmt = media_raw_audio_format::B_AUDIO_SHORT;
    *order = B_MEDIA_LITTLE_ENDIAN;
    *sample_size = 2;
    return true;
  case CUBEB_SAMPLE_S16BE:
    *fmt = media_raw_audio_format::B_AUDIO_SHORT;
    *order = B_MEDIA_BIG_ENDIAN;
    *sample_size = 2;
    return true;
  case CUBEB_SAMPLE_FLOAT32LE:
    *fmt = media_raw_audio_format::B_AUDIO_FLOAT;
    *order = B_MEDIA_LITTLE_ENDIAN;
    *sample_size = 4;
    return true;
  case CUBEB_SAMPLE_FLOAT32BE:
    *fmt = media_raw_audio_format::B_AUDIO_FLOAT;
    *order = B_MEDIA_BIG_ENDIAN;
    *sample_size = 4;
    return true;
  }
  return false;
}

static void haiku_stream_destroy(cubeb_stream * stm);

/* Connects |recorder| to a free output of the system's default audio input.
 * BMediaRecorder::Connect(format) can pick a source that isn't free ("Bad
 * source", seen with QEMU's HDA input), so name the node and output. */
static status_t
haiku_connect_default_input(BMediaRecorder * recorder,
                            const media_format & accepted)
{
  BMediaRoster * roster = BMediaRoster::Roster();
  media_node input;
  if (!roster || roster->GetAudioInput(&input) != B_OK) {
    return recorder->Connect(accepted);
  }
  media_output output;
  int32 count = 0;
  if (roster->GetFreeOutputsFor(input, &output, 1, &count,
                                B_MEDIA_RAW_AUDIO) != B_OK || count < 1) {
    roster->ReleaseNode(input);
    return recorder->Connect(accepted);
  }
  status_t status = recorder->Connect(input, &output, &accepted);
  roster->ReleaseNode(input);
  return status;
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

  if (!input_stream_params && !output_stream_params) {
    return CUBEB_ERROR_INVALID_PARAMETER;
  }
  cubeb_stream_params * params =
    output_stream_params ? output_stream_params : input_stream_params;

  uint32 fmt, order;
  size_t sample_size;
  if (!haiku_sample_format(params->format, &fmt, &order, &sample_size)) {
    return CUBEB_ERROR_INVALID_FORMAT;
  }
  /* Only native-endian input conversion is implemented. */
  if (input_stream_params &&
      input_stream_params->format != CUBEB_SAMPLE_S16NE &&
      input_stream_params->format != CUBEB_SAMPLE_FLOAT32NE) {
    return CUBEB_ERROR_INVALID_FORMAT;
  }

  cubeb_stream * stm = new cubeb_stream();
  stm->context = context;
  stm->player = NULL;
  stm->recorder = NULL;
  stm->data_callback = data_callback;
  stm->state_callback = state_callback;
  stm->user_ptr = user_ptr;
  stm->params = *params;
  stm->has_output = output_stream_params != NULL;
  stm->has_input = input_stream_params != NULL;
  stm->frame_size = sample_size * params->channels;
  /* Duplex streams may have fewer input channels (a mono microphone with
   * stereo output): the FIFO holds input-format frames. */
  stm->in_frame_size = input_stream_params
    ? (input_stream_params->format == CUBEB_SAMPLE_FLOAT32NE ? 4 : 2) *
        input_stream_params->channels
    : stm->frame_size;
  stm->frames_written = 0;
  stm->frames_read = 0;
  stm->draining = stm->drained = stm->errored = stm->running = false;
  pthread_mutex_init(&stm->mutex, NULL);

  const char * name = stream_name ? stream_name : "cubeb";

  if (stm->has_output) {
    media_raw_audio_format out = media_raw_audio_format::wildcard;
    out.format = fmt;
    out.byte_order = order;
    /* latency is in frames; use it as a hint for the buffer size, but keep
     * buffers between ~5 ms and ~50 ms. */
    unsigned int rate = params->rate;
    unsigned int buffer_frames = latency / 2;
    if (buffer_frames < rate / 200) {
      buffer_frames = rate / 200;
    }
    if (buffer_frames > rate / 20) {
      buffer_frames = rate / 20;
    }
    out.frame_rate = rate;
    out.channel_count = params->channels;
    out.buffer_size = buffer_frames * stm->frame_size;

    stm->player = new BSoundPlayer(&out, name, haiku_play_buffer, NULL, stm);
    if (stm->player->InitCheck() != B_OK) {
      haiku_stream_destroy(stm);
      return CUBEB_ERROR;
    }
  }

  if (stm->has_input) {
    stm->conv.init(input_stream_params->rate, input_stream_params->channels,
                   input_stream_params->format == CUBEB_SAMPLE_FLOAT32NE);
    stm->recorder = new BMediaRecorder(name, B_MEDIA_RAW_AUDIO);
    if (stm->recorder->InitCheck() != B_OK) {
      haiku_stream_destroy(stm);
      return CUBEB_ERROR;
    }
    media_format accepted;
    accepted.type = B_MEDIA_RAW_AUDIO;
    accepted.u.raw_audio = media_raw_audio_format::wildcard;
    stm->recorder->SetAcceptedFormat(accepted);
    if (stm->recorder->SetHooks(haiku_record_buffer, NULL, stm) != B_OK ||
        haiku_connect_default_input(stm->recorder, accepted) != B_OK) {
      haiku_stream_destroy(stm);
      return CUBEB_ERROR_DEVICE_UNAVAILABLE;
    }
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
  stm->running = false;
  if (stm->recorder) {
    if (stm->recorder->IsRunning()) {
      stm->recorder->Stop();
    }
    if (stm->recorder->IsConnected()) {
      stm->recorder->Disconnect();
    }
    delete stm->recorder;
  }
  if (stm->player) {
    stm->player->Stop(true, true);
    delete stm->player;
  }
  pthread_mutex_destroy(&stm->mutex);
  delete stm;
}

static int
haiku_stream_start(cubeb_stream * stm)
{
  stm->draining = false;
  stm->drained = false;
  stm->running = true;
  if (stm->recorder && stm->recorder->Start() != B_OK) {
    stm->running = false;
    return CUBEB_ERROR;
  }
  if (stm->player) {
    stm->player->SetHasData(true);
    if (stm->player->Start() != B_OK) {
      if (stm->recorder) {
        stm->recorder->Stop();
      }
      stm->running = false;
      return CUBEB_ERROR;
    }
  }
  stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_STARTED);
  return CUBEB_OK;
}

static int
haiku_stream_stop(cubeb_stream * stm)
{
  stm->running = false;
  if (stm->recorder && stm->recorder->IsRunning()) {
    stm->recorder->Stop();
  }
  if (stm->player) {
    stm->player->Stop(true, true);
  }
  pthread_mutex_lock(&stm->mutex);
  stm->in_fifo.clear();
  pthread_mutex_unlock(&stm->mutex);
  stm->state_callback(stm, stm->user_ptr, CUBEB_STATE_STOPPED);
  return CUBEB_OK;
}

static uint64_t
haiku_latency_frames(cubeb_stream * stm)
{
  if (!stm->player) {
    return 0;
  }
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
  uint64_t done = stm->has_output ? stm->frames_written : stm->frames_read;
  pthread_mutex_unlock(&stm->mutex);
  *position = done > lat ? done - lat : 0;
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
  if (!stm->player) {
    return CUBEB_ERROR_NOT_SUPPORTED;
  }
  stm->player->SetVolume(volume);
  return CUBEB_OK;
}

/* Device enumeration */

static cubeb_device_info *
haiku_device_info(const live_node_info & node, cubeb_device_type type,
                  bool is_default)
{
  cubeb_device_info * info =
    (cubeb_device_info *) calloc(1, sizeof(cubeb_device_info));
  if (!info) {
    return NULL;
  }
  char id[32];
  snprintf(id, sizeof(id), "haiku-node-%d", (int) node.node.node);
  info->devid = (cubeb_devid) (intptr_t) node.node.node;
  info->device_id = strdup(id);
  info->friendly_name = strdup(node.name);
  info->group_id = strdup(id);
  info->vendor_name = NULL;
  info->type = type;
  info->state = CUBEB_DEVICE_STATE_ENABLED;
  info->preferred = is_default ? CUBEB_DEVICE_PREF_ALL : CUBEB_DEVICE_PREF_NONE;
  info->format = (cubeb_device_fmt) (CUBEB_DEVICE_FMT_S16NE |
                                     CUBEB_DEVICE_FMT_F32NE);
  info->default_format = CUBEB_DEVICE_FMT_F32NE;
  info->max_channels = 2;
  info->default_rate = 48000;
  info->min_rate = 8000;
  info->max_rate = 192000;
  info->latency_lo = 256;
  info->latency_hi = 4096;
  return info;
}

static void
haiku_list_nodes(BMediaRoster * roster, uint64 kind, cubeb_device_type type,
                 const media_node & default_node,
                 std::vector<cubeb_device_info *> & out)
{
  live_node_info nodes[32];
  int32 count = 32;
  if (roster->GetLiveNodes(nodes, &count, NULL, NULL, NULL, kind) != B_OK) {
    return;
  }
  for (int32 i = 0; i < count; i++) {
    cubeb_device_info * info =
      haiku_device_info(nodes[i], type, nodes[i].node.node == default_node.node);
    if (info) {
      out.push_back(info);
    }
  }
}

static int
haiku_enumerate_devices(cubeb * context, cubeb_device_type type,
                        cubeb_device_collection ** collection)
{
  (void) context;
  status_t err;
  BMediaRoster * roster = BMediaRoster::Roster(&err);
  if (!roster || err != B_OK) {
    return CUBEB_ERROR;
  }

  std::vector<cubeb_device_info *> devices;
  media_node default_node;
  if (type & CUBEB_DEVICE_TYPE_OUTPUT) {
    default_node = media_node::null;
    roster->GetAudioOutput(&default_node);
    haiku_list_nodes(roster, B_PHYSICAL_OUTPUT, CUBEB_DEVICE_TYPE_OUTPUT,
                     default_node, devices);
    if (default_node != media_node::null) {
      roster->ReleaseNode(default_node);
    }
  }
  if (type & CUBEB_DEVICE_TYPE_INPUT) {
    default_node = media_node::null;
    roster->GetAudioInput(&default_node);
    haiku_list_nodes(roster, B_PHYSICAL_INPUT, CUBEB_DEVICE_TYPE_INPUT,
                     default_node, devices);
    if (default_node != media_node::null) {
      roster->ReleaseNode(default_node);
    }
  }

  size_t n = devices.size();
  cubeb_device_collection * coll = (cubeb_device_collection *)
    malloc(sizeof(cubeb_device_collection) +
           sizeof(cubeb_device_info *) * (n > 0 ? n - 1 : 0));
  if (!coll) {
    for (cubeb_device_info * d : devices) {
      cubeb_device_info_destroy(d);
    }
    return CUBEB_ERROR;
  }
  coll->count = n;
  for (size_t i = 0; i < n; i++) {
    coll->device[i] = devices[i];
  }
  *collection = coll;
  return CUBEB_OK;
}

static struct cubeb_ops const haiku_ops = {
  /*.init =*/ NULL,
  /*.get_backend_id =*/ haiku_get_backend_id,
  /*.get_max_channel_count =*/ haiku_get_max_channel_count,
  /*.get_min_latency =*/ haiku_get_min_latency,
  /*.get_preferred_sample_rate =*/ haiku_get_preferred_sample_rate,
  /*.enumerate_devices =*/ haiku_enumerate_devices,
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
