/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsScreenHaiku_h___
#define nsScreenHaiku_h___

#include "nsBaseScreen.h"
#include "nsIScreenManager.h"
#include "nsCOMPtr.h"

#include "nsTArray.h"

// One nsIScreen per BScreen (enumerated with BScreen::SetToNext()).
class nsScreenHaiku final : public nsBaseScreen {
 public:
  explicit nsScreenHaiku(int32_t aScreenId) : mScreenId(aScreenId) {}
  int32_t ScreenId() const { return mScreenId; }

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
  int32_t mScreenId;
};

class nsScreenManagerHaiku final : public nsIScreenManager {
 public:
  nsScreenManagerHaiku();

  NS_DECL_ISUPPORTS
  NS_DECL_NSISCREENMANAGER

 private:
  ~nsScreenManagerHaiku() = default;
  void RefreshScreens();
  nsIScreen* ScreenWithId(int32_t aId);

  nsTArray<RefPtr<nsScreenHaiku>> mScreens;
};

#endif  // nsScreenHaiku_h___
