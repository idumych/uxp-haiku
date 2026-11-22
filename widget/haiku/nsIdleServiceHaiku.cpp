/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIdleServiceHaiku.h"

#include "HaikuUtils.h"

NS_IMPL_ISUPPORTS_INHERITED0(nsIdleServiceHaiku, nsIdleService)

bool
nsIdleServiceHaiku::PollIdleTime(uint32_t* aIdleTime)
{
  // idle_time() (InterfaceDefs.h) returns microseconds since the last
  // user input, as tracked by the input_server/app_server.
  bigtime_t idle = idle_time();
  if (idle < 0) {
    return false;
  }
  *aIdleTime = uint32_t(idle / 1000);
  return true;
}

bool
nsIdleServiceHaiku::UsePollMode()
{
  return true;
}
