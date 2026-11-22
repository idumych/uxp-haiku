/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIdleServiceHaiku_h__
#define nsIdleServiceHaiku_h__

#include "nsIdleService.h"

class nsIdleServiceHaiku : public nsIdleService
{
public:
  NS_DECL_ISUPPORTS_INHERITED

  bool PollIdleTime(uint32_t* aIdleTime) override;

  static already_AddRefed<nsIdleServiceHaiku> GetInstance()
  {
    RefPtr<nsIdleService> idleService = nsIdleService::GetInstance();
    if (!idleService) {
      idleService = new nsIdleServiceHaiku();
    }
    return idleService.forget().downcast<nsIdleServiceHaiku>();
  }

protected:
  nsIdleServiceHaiku() {}
  virtual ~nsIdleServiceHaiku() {}
  bool UsePollMode() override;
};

#endif // nsIdleServiceHaiku_h__
