/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuWindowBuffer_h
#define mozilla_widget_HaikuWindowBuffer_h

#include "HaikuUtils.h"
#include "Units.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/gfx/2D.h"

class nsWindow;
class HaikuWindow;
class HaikuView;
class BView;
class BBitmap;

namespace mozilla::widget {

class HaikuWindowBuffer final {
 public:
  HaikuWindowBuffer();
  ~HaikuWindowBuffer();

  already_AddRefed<mozilla::gfx::DrawTarget> StartRemoteDrawingInRegion(
      const LayoutDeviceIntSize& aClientSize,
      const LayoutDeviceIntRegion& aInvalidRegion);

  void EndRemoteDrawingInRegion(
      nsWindow* aGeckoWindow,
      HaikuWindow* aWindow,
      HaikuView* aView,
      mozilla::gfx::DrawTarget* aDrawTarget,
      const LayoutDeviceIntRegion& aInvalidRegion);

  void DrawToView(BView* aView, BRect aRect);

  void Destroy();
  bool HasContent();

 private:
  mozilla::Mutex mRenderingLock;
  BBitmap* mBackBitmap;
  BBitmap* mFrontBitmap;
};

}  // namespace mozilla::widget

#endif  // mozilla_widget_HaikuWindowBuffer_h
