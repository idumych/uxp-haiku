/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Hal.h"

#include <stdint.h>
#include <OS.h>

namespace mozilla {
namespace hal_impl {

uint32_t
GetTotalSystemMemory()
{
  system_info info;
  if (get_system_info(&info) != B_OK) {
    return 0;
  }
  uint64_t bytes = uint64_t(info.max_pages) * B_PAGE_SIZE;
  // Hal.h's API is 32-bit; clamp like other backends would overflow.
  return bytes > UINT32_MAX ? UINT32_MAX : uint32_t(bytes);
}

} // namespace hal_impl
} // namespace mozilla
