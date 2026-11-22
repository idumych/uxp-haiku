/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsScreenHaiku_h___
#define nsScreenHaiku_h___

#include "nsBaseScreen.h"
#include "nsIScreenManager.h"
#include "nsCOMPtr.h"

// Haiku has one screen for now (B_MAIN_SCREEN_ID).
class nsScreenHaiku final : public nsBaseScreen {
 public:
  nsScreenHaiku() = default;

  NS_IMETHOD GetId(uint32_t* aId) override;
  NS_IMETHOD GetRect(int32_t* aLeft, int32_t* aTop, int32_t* aWidth,
                     int32_t* aHeight) override;
  NS_IMETHOD GetAvailRect(int32_t* aLeft, int32_t* aTop, int32_t* aWidth,
                          int32_t* aHeight) override;
  NS_IMETHOD GetPixelDepth(int32_t* aPixelDepth) override;
  NS_IMETHOD GetColorDepth(int32_t* aColorDepth) override;
  NS_IMETHOD GetDefaultCSSScaleFactor(double* aScaleFactor) override;

 private:
  ~nsScreenHaiku() = default;
};

class nsScreenManagerHaiku final : public nsIScreenManager {
 public:
  nsScreenManagerHaiku();

  NS_DECL_ISUPPORTS
  NS_DECL_NSISCREENMANAGER

 private:
  ~nsScreenManagerHaiku() = default;
  nsCOMPtr<nsIScreen> mScreen;
};

#endif  // nsScreenHaiku_h___
