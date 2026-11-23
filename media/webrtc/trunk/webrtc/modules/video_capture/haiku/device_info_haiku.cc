/*
 *  Copyright (c) 2026 The Envoi contributors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

#include "webrtc/modules/video_capture/haiku/device_info_haiku.h"

#include <stdlib.h>
#include <string.h>

#include "webrtc/modules/video_capture/haiku/haiku_video_producers.h"
#include "webrtc/modules/video_capture/video_capture_impl.h"
#include "webrtc/system_wrappers/interface/trace.h"

namespace webrtc {
namespace videocapturemodule {

VideoCaptureModule::DeviceInfo* VideoCaptureImpl::CreateDeviceInfo(
    const int32_t id) {
  return new DeviceInfoHaiku(id);
}

DeviceInfoHaiku::DeviceInfoHaiku(const int32_t id) : DeviceInfoImpl(id) {}

DeviceInfoHaiku::~DeviceInfoHaiku() {}

int32_t DeviceInfoHaiku::Init() {
  return 0;
}

uint32_t DeviceInfoHaiku::NumberOfDevices() {
  dormant_node_info nodes[kMaxHaikuVideoProducers];
  return ListHaikuVideoProducers(nodes);
}

int32_t DeviceInfoHaiku::GetDeviceName(uint32_t deviceNumber,
                                       char* deviceNameUTF8,
                                       uint32_t deviceNameLength,
                                       char* deviceUniqueIdUTF8,
                                       uint32_t deviceUniqueIdUTF8Length,
                                       char* productUniqueIdUTF8,
                                       uint32_t productUniqueIdUTF8Length,
                                       pid_t* /*pid*/) {
  dormant_node_info nodes[kMaxHaikuVideoProducers];
  int32_t count = ListHaikuVideoProducers(nodes);
  if (deviceNumber >= (uint32_t)count || !deviceNameUTF8 ||
      !deviceUniqueIdUTF8 || deviceNameLength == 0 ||
      deviceUniqueIdUTF8Length == 0) {
    return -1;
  }
  const dormant_node_info& node = nodes[deviceNumber];
  strncpy(deviceNameUTF8, node.name, deviceNameLength - 1);
  deviceNameUTF8[deviceNameLength - 1] = '\0';
  HaikuVideoProducerId(node, deviceUniqueIdUTF8, deviceUniqueIdUTF8Length);
  if (productUniqueIdUTF8 && productUniqueIdUTF8Length > 0) {
    productUniqueIdUTF8[0] = '\0';
  }
  return 0;
}

int32_t DeviceInfoHaiku::CreateCapabilityMap(const char* deviceUniqueIdUTF8) {
  dormant_node_info node;
  if (!FindHaikuVideoProducer(deviceUniqueIdUTF8, &node)) {
    WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCapture, _id,
                 "no Haiku video producer %s", deviceUniqueIdUTF8);
    return -1;
  }

  _captureCapabilities.clear();
  // The producer's own frame size when its flavor states one; sizes are
  // otherwise negotiated when it's connected, so offer common ones.
  int32_t width = 0, height = 0;
  dormant_flavor_info flavor;
  BMediaRoster* roster = BMediaRoster::Roster();
  if (roster && roster->GetDormantFlavorInfoFor(node, &flavor) == B_OK) {
    for (int32 i = 0; i < flavor.out_format_count; i++) {
      const media_format& f = flavor.out_formats[i];
      if (f.type == B_MEDIA_RAW_VIDEO && f.u.raw_video.display.line_width > 0) {
        width = f.u.raw_video.display.line_width;
        height = f.u.raw_video.display.line_count;
        break;
      }
    }
  }
  static const int32_t kSizes[][2] = { { 640, 480 }, { 320, 240 } };
  for (const auto& size : kSizes) {
    VideoCaptureCapability cap;
    cap.width = width > 0 ? width : size[0];
    cap.height = height > 0 ? height : size[1];
    cap.maxFPS = 30;
    cap.rawType = kVideoARGB;
    _captureCapabilities.push_back(cap);
    if (width > 0) {
      break;
    }
  }

  size_t length = strlen(deviceUniqueIdUTF8);
  _lastUsedDeviceNameLength = length;
  _lastUsedDeviceName = (char*)realloc(_lastUsedDeviceName, length + 1);
  memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8, length + 1);
  return _captureCapabilities.size();
}

}  // namespace videocapturemodule
}  // namespace webrtc
