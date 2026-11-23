/*
 *  Copyright (c) 2026 The Envoi contributors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_DEVICE_INFO_HAIKU_H_
#define WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_DEVICE_INFO_HAIKU_H_

#include "webrtc/modules/video_capture/device_info_impl.h"

namespace webrtc {
namespace videocapturemodule {

// Cameras on Haiku: the Media Kit's raw video producers.
class DeviceInfoHaiku : public DeviceInfoImpl {
 public:
  explicit DeviceInfoHaiku(const int32_t id);
  virtual ~DeviceInfoHaiku();
  int32_t Init();
  virtual uint32_t NumberOfDevices();
  virtual int32_t GetDeviceName(uint32_t deviceNumber,
                                char* deviceNameUTF8,
                                uint32_t deviceNameLength,
                                char* deviceUniqueIdUTF8,
                                uint32_t deviceUniqueIdUTF8Length,
                                char* productUniqueIdUTF8 = 0,
                                uint32_t productUniqueIdUTF8Length = 0,
                                pid_t* pid = 0);
  virtual int32_t CreateCapabilityMap(const char* deviceUniqueIdUTF8);
  virtual int32_t DisplayCaptureSettingsDialogBox(const char*, const char*,
                                                  void*, uint32_t, uint32_t) {
    return -1;
  }
};

}  // namespace videocapturemodule
}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_DEVICE_INFO_HAIKU_H_
