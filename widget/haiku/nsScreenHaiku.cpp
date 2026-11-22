/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsScreenHaiku.h"
#include "HaikuUtils.h"

using namespace mozilla::widget;

NS_IMETHODIMP nsScreenHaiku::GetId(uint32_t* aId) {
  *aId = 1;
  return NS_OK;
}

NS_IMETHODIMP nsScreenHaiku::GetRect(int32_t* aLeft, int32_t* aTop,
                                     int32_t* aWidth, int32_t* aHeight) {
  EnsureHaikuApplication();
  BScreen screen(B_MAIN_SCREEN_ID);
  BRect f = screen.IsValid() ? screen.Frame() : BRect(0, 0, 1023, 767);
  *aLeft = int32_t(f.left);
  *aTop = int32_t(f.top);
  *aWidth = int32_t(f.Width()) + 1;
  *aHeight = int32_t(f.Height()) + 1;
  return NS_OK;
}

NS_IMETHODIMP nsScreenHaiku::GetAvailRect(int32_t* aLeft, int32_t* aTop,
                                          int32_t* aWidth, int32_t* aHeight) {
  // The Deskbar can be anywhere; the whole screen is usable.
  return GetRect(aLeft, aTop, aWidth, aHeight);
}

NS_IMETHODIMP nsScreenHaiku::GetPixelDepth(int32_t* aPixelDepth) {
  *aPixelDepth = 24;
  return NS_OK;
}

NS_IMETHODIMP nsScreenHaiku::GetColorDepth(int32_t* aColorDepth) {
  return GetPixelDepth(aColorDepth);
}

NS_IMETHODIMP nsScreenHaiku::GetDefaultCSSScaleFactor(double* aScaleFactor) {
  *aScaleFactor = GetHaikuSystemScale();
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsScreenManagerHaiku, nsIScreenManager)

nsScreenManagerHaiku::nsScreenManagerHaiku() : mScreen(new nsScreenHaiku()) {}

NS_IMETHODIMP nsScreenManagerHaiku::ScreenForRect(int32_t, int32_t, int32_t,
                                                  int32_t,
                                                  nsIScreen** aOutScreen) {
  NS_ADDREF(*aOutScreen = mScreen);
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::ScreenForId(uint32_t aId,
                                                nsIScreen** aOutScreen) {
  NS_ADDREF(*aOutScreen = mScreen);
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::GetPrimaryScreen(nsIScreen** aPrimary) {
  NS_ADDREF(*aPrimary = mScreen);
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::GetNumberOfScreens(uint32_t* aNumber) {
  *aNumber = 1;
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::GetSystemDefaultScale(float* aScale) {
  *aScale = float(GetHaikuSystemScale());
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::ScreenForNativeWidget(
    void* aWidget, nsIScreen** aOutScreen) {
  NS_ADDREF(*aOutScreen = mScreen);
  return NS_OK;
}
