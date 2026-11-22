/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHaikuAlertsService.h"

#include "imgIContainer.h"
#include "imgIRequest.h"
#include "nsICancelable.h"
#include "nsICommandLine.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/widget/HaikuRemote.h"

#pragma GCC visibility push(default)
#include <app/Notification.h>
#include <interface/Bitmap.h>
#pragma GCC visibility pop

using namespace mozilla;
using namespace mozilla::gfx;

// Must match the BApplication signature in widget/haiku/HaikuApplication.cpp.
#define ALERT_CLICK_FLAG "haiku-alert-click"

// How long a shown alert stays clickable before "alertfinished" is sent.
static const uint32_t kAlertLifetimeMs = 5 * 60 * 1000;
static const int32_t kMaxIconSize = 64;

// One alert: loads the image, then asks the service to send it; owns the
// listener and the lifetime timer.
class nsHaikuAlert final : public nsIAlertNotificationImageListener,
                           public nsITimerCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIALERTNOTIFICATIONIMAGELISTENER
  NS_DECL_NSITIMERCALLBACK

  nsHaikuAlert(nsHaikuAlertsService* aService, const nsAString& aName,
               nsIObserver* aListener)
    : mService(aService), mName(aName), mListener(aListener), mDone(false) {}

  nsresult Init(nsIAlertNotification* aAlert)
  {
    aAlert->GetTitle(mTitle);
    aAlert->GetText(mText);
    aAlert->GetCookie(mCookie);
    aAlert->GetTextClickable(&mClickable);
    nsCOMPtr<nsICancelable> request;
    nsresult rv = aAlert->LoadImage(10000, this, nullptr,
                                    getter_AddRefs(request));
    if (NS_FAILED(rv)) {
      return OnImageMissing(nullptr);
    }
    mImageRequest = request;
    return NS_OK;
  }

  void Fire(const char* aTopic)
  {
    if (mListener) {
      mListener->Observe(nullptr, aTopic, mCookie.get());
    }
  }

  void Clicked()
  {
    if (mDone) {
      return;
    }
    if (mClickable) {
      Fire("alertclickcallback");
    }
    Finish();
  }

  void Finish()
  {
    if (mDone) {
      return;
    }
    mDone = true;
    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }
    if (mImageRequest) {
      mImageRequest->Cancel(NS_BINDING_ABORTED);
      mImageRequest = nullptr;
    }
    Fire("alertfinished");
    mListener = nullptr;
  }

  void Shown()
  {
    Fire("alertshow");
    mTimer = do_CreateInstance(NS_TIMER_CONTRACTID);
    if (mTimer) {
      mTimer->InitWithCallback(this, kAlertLifetimeMs,
                               nsITimer::TYPE_ONE_SHOT);
    }
  }

  RefPtr<nsHaikuAlertsService> mService;
  nsString mName;
  nsString mTitle;
  nsString mText;
  nsString mCookie;
  bool mClickable;
  nsCOMPtr<nsIObserver> mListener;
  nsCOMPtr<nsICancelable> mImageRequest;
  nsCOMPtr<nsITimer> mTimer;
  bool mDone;

private:
  ~nsHaikuAlert() {}
};

NS_IMPL_ISUPPORTS(nsHaikuAlert, nsIAlertNotificationImageListener,
                  nsITimerCallback)

NS_IMETHODIMP
nsHaikuAlert::OnImageMissing(nsISupports* aUserData)
{
  mImageRequest = nullptr;
  if (!mDone) {
    mService->Send(this, nullptr, 0, 0, 0);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHaikuAlert::OnImageReady(nsISupports* aUserData, imgIRequest* aRequest)
{
  mImageRequest = nullptr;
  if (mDone) {
    return NS_OK;
  }
  nsCOMPtr<imgIContainer> image;
  if (NS_FAILED(aRequest->GetImage(getter_AddRefs(image))) || !image) {
    return OnImageMissing(aUserData);
  }
  RefPtr<SourceSurface> surface =
    image->GetFrame(imgIContainer::FRAME_FIRST,
                    imgIContainer::FLAG_SYNC_DECODE);
  RefPtr<DataSourceSurface> data = surface ? surface->GetDataSurface() : nullptr;
  if (!data || (data->GetFormat() != SurfaceFormat::B8G8R8A8 &&
                data->GetFormat() != SurfaceFormat::B8G8R8X8)) {
    return OnImageMissing(aUserData);
  }
  DataSourceSurface::ScopedMap map(data, DataSourceSurface::READ);
  if (!map.IsMapped()) {
    return OnImageMissing(aUserData);
  }
  IntSize size = data->GetSize();
  mService->Send(this, map.GetData(), size.width, size.height,
                 map.GetStride());
  return NS_OK;
}

NS_IMETHODIMP
nsHaikuAlert::Notify(nsITimer* aTimer)
{
  mTimer = nullptr;
  nsString name(mName);
  RefPtr<nsHaikuAlertsService> service = mService;
  Finish();
  service->Finished(name);
  return NS_OK;
}

// nsHaikuAlertsService

NS_IMPL_ISUPPORTS(nsHaikuAlertsService, nsIAlertsService,
                  nsICommandLineHandler)

nsHaikuAlertsService::nsHaikuAlertsService()
  : mCounter(0)
{
}

nsHaikuAlertsService::~nsHaikuAlertsService()
{
}

static BBitmap*
MakeIcon(uint8_t* aBGRA, int32_t aWidth, int32_t aHeight, int32_t aStride)
{
  if (!aBGRA || aWidth <= 0 || aHeight <= 0) {
    return nullptr;
  }
  // Nearest-neighbour downscale to fit kMaxIconSize, keeping the aspect.
  int32_t w = aWidth, h = aHeight;
  if (w > kMaxIconSize || h > kMaxIconSize) {
    if (w >= h) {
      h = std::max(1, h * kMaxIconSize / w);
      w = kMaxIconSize;
    } else {
      w = std::max(1, w * kMaxIconSize / h);
      h = kMaxIconSize;
    }
  }
  BBitmap* bitmap = new BBitmap(BRect(0, 0, w - 1, h - 1), B_RGBA32);
  if (bitmap->InitCheck() != B_OK) {
    delete bitmap;
    return nullptr;
  }
  uint8* dst = (uint8*) bitmap->Bits();
  int32 dstStride = bitmap->BytesPerRow();
  for (int32_t y = 0; y < h; y++) {
    const uint8_t* srcRow = aBGRA + (int64_t(y) * aHeight / h) * aStride;
    uint8* dstRow = dst + y * dstStride;
    for (int32_t x = 0; x < w; x++) {
      const uint8_t* s = srcRow + (int64_t(x) * aWidth / w) * 4;
      uint8 a = s[3];
      // Gecko surfaces are premultiplied; B_RGBA32 is not.
      for (int c = 0; c < 3; c++) {
        dstRow[x * 4 + c] = a ? std::min(255, s[c] * 255 / a) : 0;
      }
      dstRow[x * 4 + 3] = a;
    }
  }
  return bitmap;
}

void
nsHaikuAlertsService::Send(nsHaikuAlert* aAlert, uint8_t* aBGRA,
                           int32_t aWidth, int32_t aHeight, int32_t aStride)
{
  BNotification notification(B_INFORMATION_NOTIFICATION);
  NS_ConvertUTF16toUTF8 name(aAlert->mName);
  NS_ConvertUTF16toUTF8 title(aAlert->mTitle);
  NS_ConvertUTF16toUTF8 text(aAlert->mText);

  notification.SetGroup(mozilla::widget::HaikuAppName());
  notification.SetMessageID(name.get());
  notification.SetTitle(title.IsEmpty() ? mozilla::widget::HaikuAppName()
                                         : title.get());
  notification.SetContent(text.get());
  notification.SetOnClickApp(mozilla::widget::HaikuAppSignature());
  notification.AddOnClickArg(BString("-" ALERT_CLICK_FLAG));
  notification.AddOnClickArg(BString(name.get()));

  BBitmap* icon = MakeIcon(aBGRA, aWidth, aHeight, aStride);
  if (icon) {
    notification.SetIcon(icon);  // copies the bitmap
    delete icon;
  }

  if (notification.Send() != B_OK) {
    nsString alertName(aAlert->mName);
    aAlert->Finish();
    Finished(alertName);
    return;
  }
  aAlert->Shown();
}

void
nsHaikuAlertsService::Finished(const nsAString& aName)
{
  mAlerts.Remove(aName);
}

NS_IMETHODIMP
nsHaikuAlertsService::ShowAlertNotification(const nsAString& aImageUrl,
                                            const nsAString& aAlertTitle,
                                            const nsAString& aAlertText,
                                            bool aAlertTextClickable,
                                            const nsAString& aAlertCookie,
                                            nsIObserver* aAlertListener,
                                            const nsAString& aAlertName,
                                            const nsAString& aBidi,
                                            const nsAString& aLang,
                                            const nsAString& aData,
                                            nsIPrincipal* aPrincipal,
                                            bool aInPrivateBrowsing,
                                            bool aRequireInteraction)
{
  nsCOMPtr<nsIAlertNotification> alert =
    do_CreateInstance(ALERT_NOTIFICATION_CONTRACTID);
  NS_ENSURE_TRUE(alert, NS_ERROR_FAILURE);
  nsresult rv = alert->Init(aAlertName, aImageUrl, aAlertTitle,
                            aAlertText, aAlertTextClickable,
                            aAlertCookie, aBidi, aLang, aData,
                            aPrincipal, aInPrivateBrowsing,
                            aRequireInteraction);
  NS_ENSURE_SUCCESS(rv, rv);
  return ShowAlert(alert, aAlertListener);
}

NS_IMETHODIMP
nsHaikuAlertsService::ShowPersistentNotification(const nsAString& aPersistentData,
                                                 nsIAlertNotification* aAlert,
                                                 nsIObserver* aAlertListener)
{
  return ShowAlert(aAlert, aAlertListener);
}

NS_IMETHODIMP
nsHaikuAlertsService::ShowAlert(nsIAlertNotification* aAlert,
                                nsIObserver* aAlertListener)
{
  NS_ENSURE_ARG(aAlert);
  nsAutoString name;
  nsresult rv = aAlert->GetName(name);
  NS_ENSURE_SUCCESS(rv, rv);
  if (name.IsEmpty()) {
    name.AppendLiteral("basilisk-alert-");
    name.AppendInt(++mCounter);
  }

  // An alert with the same name replaces the previous one.
  RefPtr<nsHaikuAlert> old;
  if (mAlerts.Get(name, getter_AddRefs(old))) {
    mAlerts.Remove(name);
    old->Finish();
  }

  RefPtr<nsHaikuAlert> alert = new nsHaikuAlert(this, name, aAlertListener);
  mAlerts.Put(name, alert);
  return alert->Init(aAlert);
}

NS_IMETHODIMP
nsHaikuAlertsService::CloseAlert(const nsAString& aAlertName,
                                 nsIPrincipal* aPrincipal)
{
  // The notification_server has no API to retract a shown notification;
  // just tell the listener the alert is gone.
  RefPtr<nsHaikuAlert> entry;
  if (mAlerts.Get(aAlertName, getter_AddRefs(entry))) {
    mAlerts.Remove(aAlertName);
    entry->Finish();
  }
  return NS_OK;
}

// nsICommandLineHandler: "-haiku-alert-click <name>" from a notification.

NS_IMETHODIMP
nsHaikuAlertsService::Handle(nsICommandLine* aCmdLine)
{
  nsAutoString name;
  nsresult rv = aCmdLine->HandleFlagWithParam(NS_LITERAL_STRING(ALERT_CLICK_FLAG),
                                              false, name);
  if (NS_FAILED(rv) || name.IsEmpty()) {
    return NS_OK;
  }
  RefPtr<nsHaikuAlert> entry;
  if (!mAlerts.Get(name, getter_AddRefs(entry))) {
    // Stale alert (e.g. from a previous session): just start normally.
    return NS_OK;
  }
  mAlerts.Remove(name);
  entry->Clicked();
  // A click on a live alert must not also open a new window.
  aCmdLine->SetPreventDefault(true);
  return NS_OK;
}

NS_IMETHODIMP
nsHaikuAlertsService::GetHelpInfo(nsACString& aResult)
{
  aResult.Truncate();
  return NS_OK;
}
