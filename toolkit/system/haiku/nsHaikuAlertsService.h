/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHaikuAlertsService_h__
#define nsHaikuAlertsService_h__

#include "nsIAlertsService.h"
#include "nsICommandLineHandler.h"
#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsCOMPtr.h"
#include "nsRefPtrHashtable.h"
#include "nsString.h"
#include "mozilla/RefPtr.h"

class nsHaikuAlert;

// System alerts backend for Haiku: alerts are BNotifications shown by the
// notification_server. A click relaunches the app (B_ARGV_RECEIVED in the
// running instance) with "-haiku-alert-click <name>", which this service
// also handles as a command-line handler to fire "alertclickcallback".
class nsHaikuAlertsService final : public nsIAlertsService,
                                   public nsICommandLineHandler
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIALERTSSERVICE
  NS_DECL_NSICOMMANDLINEHANDLER

  nsHaikuAlertsService();

  // Called by nsHaikuAlert once the image (if any) is ready.
  void Send(nsHaikuAlert* aAlert, uint8_t* aBGRA, int32_t aWidth,
            int32_t aHeight, int32_t aStride);
  void Finished(const nsAString& aName);

private:
  ~nsHaikuAlertsService();

  nsRefPtrHashtable<nsStringHashKey, nsHaikuAlert> mAlerts;
  uint32_t mCounter;
};

#endif /* nsHaikuAlertsService_h__ */
