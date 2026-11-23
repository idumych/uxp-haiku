/*
 *  Copyright (c) 2026 The Envoi contributors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

#include "webrtc/modules/video_capture/haiku/video_capture_haiku.h"

#include <media/MediaRecorder.h>

#include <new>
#include <string.h>

#include "webrtc/modules/video_capture/haiku/haiku_video_producers.h"
#include "webrtc/system_wrappers/interface/critical_section_wrapper.h"
#include "webrtc/system_wrappers/interface/ref_count.h"
#include "webrtc/system_wrappers/interface/trace.h"

namespace webrtc {
namespace videocapturemodule {

VideoCaptureModule* VideoCaptureImpl::Create(const int32_t id,
                                             const char* deviceUniqueId) {
  RefCountImpl<VideoCaptureModuleHaiku>* implementation =
      new RefCountImpl<VideoCaptureModuleHaiku>(id);
  if (!implementation || implementation->Init(deviceUniqueId) != 0) {
    delete implementation;
    implementation = nullptr;
  }
  return implementation;
}

VideoCaptureModuleHaiku::VideoCaptureModuleHaiku(int32_t id)
    : VideoCaptureImpl(id),
      _recorder(nullptr),
      _captureCritSect(CriticalSectionWrapper::CreateCriticalSection()),
      _captureStarted(false) {}

VideoCaptureModuleHaiku::~VideoCaptureModuleHaiku() {
  StopCapture();
  delete _captureCritSect;
}

int32_t VideoCaptureModuleHaiku::Init(const char* deviceUniqueIdUTF8) {
  if (!FindHaikuVideoProducer(deviceUniqueIdUTF8, &_node)) {
    WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id,
                 "no Haiku video producer %s", deviceUniqueIdUTF8);
    return -1;
  }
  size_t length = strlen(deviceUniqueIdUTF8);
  _deviceUniqueId = new (std::nothrow) char[length + 1];
  if (!_deviceUniqueId) {
    return -1;
  }
  memcpy(_deviceUniqueId, deviceUniqueIdUTF8, length + 1);
  return 0;
}

int32_t VideoCaptureModuleHaiku::StartCapture(
    const VideoCaptureCapability& capability) {
  {
    CriticalSectionScoped cs(_captureCritSect);
    if (_captureStarted &&
        capability.width == _requestedCapability.width &&
        capability.height == _requestedCapability.height &&
        capability.maxFPS == _requestedCapability.maxFPS) {
      return 0;
    }
  }
  StopCapture();

  CriticalSectionScoped cs(_captureCritSect);
  _recorder = new (std::nothrow) BMediaRecorder("Envoi camera",
                                                B_MEDIA_RAW_VIDEO);
  if (!_recorder || _recorder->InitCheck() != B_OK ||
      _recorder->SetHooks(RecordBuffer, nullptr, this) != B_OK) {
    delete _recorder;
    _recorder = nullptr;
    return -1;
  }

  // Ask for 32-bit RGB at the requested size; take whatever the producer
  // offers if it can't.
  media_format format;
  format.type = B_MEDIA_RAW_VIDEO;
  format.u.raw_video = media_raw_video_format::wildcard;
  format.u.raw_video.display.format = B_RGB32;
  if (capability.width > 0 && capability.height > 0) {
    format.u.raw_video.display.line_width = capability.width;
    format.u.raw_video.display.line_count = capability.height;
  }
  status_t status = _recorder->Connect(_node, format);
  if (status != B_OK) {
    format.u.raw_video = media_raw_video_format::wildcard;
    status = _recorder->Connect(_node, format);
  }
  if (status != B_OK || _recorder->Start() != B_OK) {
    WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id,
                 "can't capture from %s: %s", _node.name, strerror(status));
    delete _recorder;
    _recorder = nullptr;
    return -1;
  }

  _requestedCapability = capability;
  _currentSettings = capability;
  _captureStarted = true;
  return 0;
}

int32_t VideoCaptureModuleHaiku::StopCapture() {
  BMediaRecorder* recorder;
  {
    CriticalSectionScoped cs(_captureCritSect);
    recorder = _recorder;
    _recorder = nullptr;
    _captureStarted = false;
  }
  // Outside the lock: stopping waits for a buffer callback that may be
  // waiting for the lock in OnBuffer.
  if (recorder) {
    recorder->Stop();
    recorder->Disconnect();
    delete recorder;
  }
  return 0;
}

bool VideoCaptureModuleHaiku::CaptureStarted() {
  CriticalSectionScoped cs(_captureCritSect);
  return _captureStarted;
}

int32_t VideoCaptureModuleHaiku::CaptureSettings(
    VideoCaptureCapability& settings) {
  CriticalSectionScoped cs(_captureCritSect);
  settings = _currentSettings;
  return 0;
}

/* static */ void VideoCaptureModuleHaiku::RecordBuffer(
    void* cookie, bigtime_t /*timestamp*/, void* data, size_t size,
    const media_format& format) {
  static_cast<VideoCaptureModuleHaiku*>(cookie)->OnBuffer(data, size, format);
}

void VideoCaptureModuleHaiku::OnBuffer(void* data, size_t size,
                                       const media_format& format) {
  if (format.type != B_MEDIA_RAW_VIDEO || !data) {
    return;
  }
  const media_video_display_info& display = format.u.raw_video.display;
  int32_t width = display.line_width;
  int32_t height = display.line_count;
  RawVideoType type;
  int32_t bytesPerPixel;
  switch (display.format) {
    case B_RGB32:
    case B_RGBA32:  // B, G, R, A in memory: libyuv's ARGB
      type = kVideoARGB;
      bytesPerPixel = 4;
      break;
    case B_RGB24:   // B, G, R: libyuv's RGB24
      type = kVideoRGB24;
      bytesPerPixel = 3;
      break;
    case B_YCbCr422:  // Y0 Cb Y1 Cr: YUY2
      type = kVideoYUY2;
      bytesPerPixel = 2;
      break;
    default:
      return;
  }
  if (width <= 0 || height <= 0) {
    return;
  }
  size_t rowBytes = (size_t)width * bytesPerPixel;
  size_t stride = display.bytes_per_row ? display.bytes_per_row : rowBytes;
  if (stride < rowBytes || stride * height > size) {
    return;
  }

  uint8_t* frame = static_cast<uint8_t*>(data);
  if (stride != rowBytes) {
    _packed.resize(rowBytes * height);
    for (int32_t y = 0; y < height; y++) {
      memcpy(&_packed[y * rowBytes], frame + y * stride, rowBytes);
    }
    frame = _packed.data();
  }

  VideoCaptureCapability frameInfo;
  frameInfo.width = width;
  frameInfo.height = height;
  frameInfo.rawType = type;
  {
    CriticalSectionScoped cs(_captureCritSect);
    if (!_captureStarted) {
      return;
    }
    frameInfo.maxFPS = _requestedCapability.maxFPS;
    _currentSettings.width = width;
    _currentSettings.height = height;
    _currentSettings.rawType = type;
  }
  IncomingFrame(frame, rowBytes * height, frameInfo);
}

}  // namespace videocapturemodule
}  // namespace webrtc
