/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSNETWORKLINKSERVICEHAIKU_H_
#define NSNETWORKLINKSERVICEHAIKU_H_

#include "nsINetworkLinkService.h"
#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsCOMPtr.h"

// Haiku network link service: polls the interface list with BNetworkRoster
// on a main-thread timer and reports link up/down changes.
class nsNetworkLinkService : public nsINetworkLinkService,
                             public nsIObserver,
                             public nsITimerCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSINETWORKLINKSERVICE
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK

  nsNetworkLinkService();

  nsresult Init();
  nsresult Shutdown();

protected:
  virtual ~nsNetworkLinkService();

private:
  void CheckLink(bool aNotify);
  void SendEvent(const char* aEventID);

  bool mLinkUp;
  bool mStatusKnown;
  nsCOMPtr<nsITimer> mTimer;
};

#endif /* NSNETWORKLINKSERVICEHAIKU_H_ */
