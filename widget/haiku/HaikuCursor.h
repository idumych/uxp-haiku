/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuCursor_h
#define mozilla_widget_HaikuCursor_h

#include "HaikuUtils.h"
#include "nsIWidget.h"
#include "mozilla/UniquePtr.h"

class imgIContainer;

namespace mozilla {
namespace widget {

/* Maps Gecko cursors onto Haiku BCursors and applies them to a view.
 * The caller must not hold the view's window lock; SetCursor locks it. */
class HaikuCursor
{
public:
  static BCursorID CursorIDFor(nsCursor aCursor);

  // Sets a system cursor on aView.
  static void SetCursor(BView* aView, nsCursor aCursor);

  // Sets an image cursor on aView. aHolder keeps the BCursor alive and is
  // replaced on each call. Returns false if the image could not be used.
  static bool SetImageCursor(BView* aView, imgIContainer* aImage,
                             uint32_t aHotspotX, uint32_t aHotspotY,
                             UniquePtr<BCursor>& aHolder);

private:
  static const BCursor* GetSystemCursor(BCursorID aId);
  static void Apply(BView* aView, const BCursor* aCursor);
};

} // namespace widget
} // namespace mozilla

#endif // mozilla_widget_HaikuCursor_h
