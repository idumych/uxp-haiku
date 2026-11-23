/*
 *  Copyright (c) 2026 The Envoi contributors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

// Haiku: the cameras are the Media Kit's dormant nodes that produce raw video
// and consume nothing (webcam add-ons, the demo video producer); filters such
// as the video mixer also consume, so they're left out.

#ifndef WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_HAIKU_VIDEO_PRODUCERS_H_
#define WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_HAIKU_VIDEO_PRODUCERS_H_

#include <media/MediaAddOn.h>
#include <media/MediaDefs.h>
#include <media/MediaRoster.h>

#include <stdio.h>
#include <string.h>

namespace webrtc {
namespace videocapturemodule {

enum { kMaxHaikuVideoProducers = 16 };

// Fills |nodes| and returns how many there are.
static inline int32_t ListHaikuVideoProducers(dormant_node_info* nodes) {
  BMediaRoster* roster = BMediaRoster::Roster();
  if (!roster) {
    return 0;
  }
  media_format format;
  format.type = B_MEDIA_RAW_VIDEO;
  format.u.raw_video = media_raw_video_format::wildcard;
  int32 count = kMaxHaikuVideoProducers;
  if (roster->GetDormantNodes(nodes, &count, nullptr, &format, nullptr,
                              B_BUFFER_PRODUCER, B_BUFFER_CONSUMER) != B_OK) {
    return 0;
  }
  return count;
}

// The device's unique id: "<add-on id>:<flavor id>".
static inline void HaikuVideoProducerId(const dormant_node_info& node,
                                        char* id, size_t size) {
  snprintf(id, size, "%d:%d", (int)node.addon, (int)node.flavor_id);
}

static inline bool FindHaikuVideoProducer(const char* id,
                                          dormant_node_info* found) {
  dormant_node_info nodes[kMaxHaikuVideoProducers];
  int32_t count = ListHaikuVideoProducers(nodes);
  for (int32_t i = 0; i < count; i++) {
    char nodeId[32];
    HaikuVideoProducerId(nodes[i], nodeId, sizeof(nodeId));
    if (id && strcmp(id, nodeId) == 0) {
      *found = nodes[i];
      return true;
    }
  }
  return false;
}

}  // namespace videocapturemodule
}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CAPTURE_HAIKU_HAIKU_VIDEO_PRODUCERS_H_
