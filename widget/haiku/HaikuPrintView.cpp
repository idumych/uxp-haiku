/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* The BView that draws a page for BPrintJob. Built with -frtti (libbe
 * dynamic_casts views); keep Gecko polymorphic types out of this file. */

#include "HaikuPrintView.h"

#include <interface/Bitmap.h>
#include <interface/View.h>

namespace mozilla {
namespace widget {

// A hidden window whose view draws one page bitmap for BPrintJob.
class PrintPageView : public BView {
 public:
  explicit PrintPageView(BRect aFrame)
      : BView(aFrame, "page", B_FOLLOW_NONE, B_WILL_DRAW) {}

  void Draw(BRect aUpdate) override {
    if (mPage) {
      DrawBitmap(mPage, mPage->Bounds(), mDest,
                 B_FILTER_BITMAP_BILINEAR);
    }
  }

  BBitmap* mPage = nullptr;
  BRect mDest;
};

BView* CreatePrintPageView(BRect aFrame)
{
  return new PrintPageView(aFrame);
}

void SetPrintPage(BView* aView, BBitmap* aPage, BRect aDest)
{
  auto* view = static_cast<PrintPageView*>(aView);
  view->mPage = aPage;
  view->mDest = aDest;
}

} // namespace widget
} // namespace mozilla
