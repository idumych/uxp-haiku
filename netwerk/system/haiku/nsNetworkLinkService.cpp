/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNetworkLinkService.h"

#include "nsIObserverService.h"
#include "nsServiceManagerUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsString.h"
#include "mozilla/Services.h"

#include <net/if.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>

static const uint32_t kPollIntervalMs = 5000;

NS_IMPL_ISUPPORTS(nsNetworkLinkService,
                  nsINetworkLinkService,
                  nsIObserver,
                  nsITimerCallback)

nsNetworkLinkService::nsNetworkLinkService()
  : mLinkUp(true)
  , mStatusKnown(false)
{
}

nsNetworkLinkService::~nsNetworkLinkService()
{
}

NS_IMETHODIMP
nsNetworkLinkService::GetIsLinkUp(bool* aIsUp)
{
  *aIsUp = mLinkUp;
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetLinkStatusKnown(bool* aIsKnown)
{
  *aIsKnown = mStatusKnown;
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::GetLinkType(uint32_t* aLinkType)
{
  NS_ENSURE_ARG_POINTER(aLinkType);
  *aLinkType = nsINetworkLinkService::LINK_TYPE_UNKNOWN;
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::Observe(nsISupports* aSubject, const char* aTopic,
                              const char16_t* aData)
{
  if (!strcmp(aTopic, "xpcom-shutdown")) {
    Shutdown();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNetworkLinkService::Notify(nsITimer* aTimer)
{
  CheckLink(true);
  return NS_OK;
}

nsresult
nsNetworkLinkService::Init()
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (!observerService) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = observerService->AddObserver(this, "xpcom-shutdown", false);
  NS_ENSURE_SUCCESS(rv, rv);

  CheckLink(false);

  mTimer = do_CreateInstance(NS_TIMER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return mTimer->InitWithCallback(this, kPollIntervalMs,
                                  nsITimer::TYPE_REPEATING_SLACK);
}

nsresult
nsNetworkLinkService::Shutdown()
{
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    observerService->RemoveObserver(this, "xpcom-shutdown");
  }
  return NS_OK;
}

void
nsNetworkLinkService::CheckLink(bool aNotify)
{
  bool up = false;
  BNetworkRoster& roster = BNetworkRoster::Default();
  BNetworkInterface iface;
  uint32 cookie = 0;
  while (roster.GetNextInterface(&cookie, iface) == B_OK) {
    uint32 flags = iface.Flags();
    if ((flags & IFF_LOOPBACK) == 0 && (flags & IFF_UP) != 0 &&
        (flags & IFF_LINK) != 0 && iface.CountAddresses() > 0) {
      up = true;
      break;
    }
  }

  bool wasKnown = mStatusKnown;
  bool changed = !wasKnown || up != mLinkUp;
  mLinkUp = up;
  mStatusKnown = true;
  if (aNotify && changed) {
    SendEvent(up ? NS_NETWORK_LINK_DATA_UP : NS_NETWORK_LINK_DATA_DOWN);
  }
}

void
nsNetworkLinkService::SendEvent(const char* aEventID)
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (!observerService) {
    return;
  }
  observerService->NotifyObservers(static_cast<nsINetworkLinkService*>(this),
                                   NS_NETWORK_LINK_TOPIC,
                                   NS_ConvertASCIItoUTF16(aEventID).get());
}
