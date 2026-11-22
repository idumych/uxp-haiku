/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsScreenHaiku.h"
#include "HaikuUtils.h"

#include <algorithm>

using namespace mozilla::widget;

static BRect ScreenFrame(int32_t aId) {
  EnsureHaikuApplication();
  BScreen screen{screen_id{aId}};
  return screen.IsValid() ? screen.Frame() : BRect(0, 0, 1023, 767);
}

NS_IMETHODIMP nsScreenHaiku::GetId(uint32_t* aId) {
  *aId = uint32_t(mScreenId) + 1;
  return NS_OK;
}

NS_IMETHODIMP nsScreenHaiku::GetRect(int32_t* aLeft, int32_t* aTop,
                                     int32_t* aWidth, int32_t* aHeight) {
  BRect f = ScreenFrame(mScreenId);
  *aLeft = int32_t(f.left);
  *aTop = int32_t(f.top);
  *aWidth = int32_t(f.Width()) + 1;
  *aHeight = int32_t(f.Height()) + 1;
  return NS_OK;
}

NS_IMETHODIMP nsScreenHaiku::GetAvailRect(int32_t* aLeft, int32_t* aTop,
                                          int32_t* aWidth, int32_t* aHeight) {
  // The Deskbar can be anywhere and windows may overlap it.
  return GetRect(aLeft, aTop, aWidth, aHeight);
}

NS_IMETHODIMP nsScreenHaiku::GetPixelDepth(int32_t* aPixelDepth) {
  EnsureHaikuApplication();
  BScreen screen{screen_id{mScreenId}};
  color_space cs = screen.IsValid() ? screen.ColorSpace() : B_RGB32;
  *aPixelDepth = (cs == B_RGB16 || cs == B_RGB15) ? 16
                 : (cs == B_CMAP8) ? 8 : 24;
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

nsScreenManagerHaiku::nsScreenManagerHaiku() { RefreshScreens(); }

void nsScreenManagerHaiku::RefreshScreens() {
  EnsureHaikuApplication();
  nsTArray<RefPtr<nsScreenHaiku>> screens;
  BScreen screen(B_MAIN_SCREEN_ID);
  while (screen.IsValid()) {
    int32_t id = screen.ID().id;
    RefPtr<nsScreenHaiku> existing;
    for (auto& s : mScreens) {
      if (s->ScreenId() == id) {
        existing = s;
      }
    }
    if (!existing) {
      existing = new nsScreenHaiku(id);
    }
    screens.AppendElement(existing);
    if (screen.SetToNext() != B_OK) {
      break;
    }
  }
  if (screens.IsEmpty()) {
    screens.AppendElement(new nsScreenHaiku(B_MAIN_SCREEN_ID.id));
  }
  mScreens = std::move(screens);
}

nsIScreen* nsScreenManagerHaiku::ScreenWithId(int32_t aId) {
  for (auto& s : mScreens) {
    if (s->ScreenId() == aId) {
      return s;
    }
  }
  return mScreens[0];
}

NS_IMETHODIMP nsScreenManagerHaiku::ScreenForRect(int32_t aLeft, int32_t aTop,
                                                  int32_t aWidth,
                                                  int32_t aHeight,
                                                  nsIScreen** aOutScreen) {
  RefreshScreens();
  BRect rect(aLeft, aTop, aLeft + std::max(aWidth, 1) - 1,
             aTop + std::max(aHeight, 1) - 1);
  nsIScreen* best = mScreens[0];
  float bestArea = -1;
  for (auto& s : mScreens) {
    BRect overlap = rect & ScreenFrame(s->ScreenId());
    float area = overlap.IsValid() ? (overlap.Width() + 1) *
                                         (overlap.Height() + 1) : 0;
    if (area > bestArea) {
      bestArea = area;
      best = s;
    }
  }
  NS_ADDREF(*aOutScreen = best);
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::ScreenForId(uint32_t aId,
                                                nsIScreen** aOutScreen) {
  RefreshScreens();
  for (auto& s : mScreens) {
    if (uint32_t(s->ScreenId()) + 1 == aId) {
      NS_ADDREF(*aOutScreen = s);
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsScreenManagerHaiku::GetPrimaryScreen(nsIScreen** aPrimary) {
  RefreshScreens();
  NS_ADDREF(*aPrimary = ScreenWithId(B_MAIN_SCREEN_ID.id));
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::GetNumberOfScreens(uint32_t* aNumber) {
  RefreshScreens();
  *aNumber = mScreens.Length();
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::GetSystemDefaultScale(float* aScale) {
  *aScale = float(GetHaikuSystemScale());
  return NS_OK;
}

NS_IMETHODIMP nsScreenManagerHaiku::ScreenForNativeWidget(
    void* aWidget, nsIScreen** aOutScreen) {
  RefreshScreens();
  int32_t id = B_MAIN_SCREEN_ID.id;
  if (aWidget) {
    BScreen screen(static_cast<BWindow*>(aWidget));
    if (screen.IsValid()) {
      id = screen.ID().id;
    }
  }
  NS_ADDREF(*aOutScreen = ScreenWithId(id));
  return NS_OK;
}
