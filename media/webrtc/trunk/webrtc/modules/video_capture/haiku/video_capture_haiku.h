/*
 *  Copyright (c) 2026 The Envoi contributors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_VIDEO_CAPTURE_HAIKU_H_
#define WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_VIDEO_CAPTURE_HAIKU_H_

#include <media/MediaAddOn.h>
#include <media/MediaDefs.h>
#include <media/MediaRecorder.h>

#include <vector>

#include "webrtc/modules/video_capture/video_capture_impl.h"

// (No forward declaration of BMediaRecorder: declared outside the system
// header wrappers, it would be hidden and break linking against libmedia.)

namespace webrtc {
class CriticalSectionWrapper;
namespace videocapturemodule {

// Captures from a Media Kit video producer through a BMediaRecorder, which
// instantiates the producer, connects to it and hands over each buffer.
class VideoCaptureModuleHaiku : public VideoCaptureImpl {
 public:
  explicit VideoCaptureModuleHaiku(int32_t id);
  virtual ~VideoCaptureModuleHaiku();
  int32_t Init(const char* deviceUniqueId);
  virtual int32_t StartCapture(const VideoCaptureCapability& capability);
  virtual int32_t StopCapture();
  virtual bool CaptureStarted();
  virtual int32_t CaptureSettings(VideoCaptureCapability& settings);

 private:
  static void RecordBuffer(void* cookie, bigtime_t timestamp, void* data,
                           size_t size, const media_format& format);
  void OnBuffer(void* data, size_t size, const media_format& format);

  dormant_node_info _node;
  BMediaRecorder* _recorder;
  CriticalSectionWrapper* _captureCritSect;
  bool _captureStarted;
  VideoCaptureCapability _currentSettings;
  std::vector<uint8_t> _packed;  // rows without padding, for IncomingFrame
};

}  // namespace videocapturemodule
}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_VIDEO_CAPTURE_HAIKU_H_
