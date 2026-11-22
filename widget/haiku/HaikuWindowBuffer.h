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
// Haiku API classes: declare them with default visibility, or gcc_hidden.h makes
// them hidden and libxul can't link against libbe.
#pragma GCC visibility push(default)
class BView;
class BBitmap;
#pragma GCC visibility pop

namespace mozilla::widget {

class HaikuWindowBuffer final {
 public:
  HaikuWindowBuffer();
  ~HaikuWindowBuffer();

  already_AddRefed<mozilla::gfx::DrawTarget> StartRemoteDrawingInRegion(
      const LayoutDeviceIntSize& aClientSize,
      const LayoutDeviceIntRegion& aInvalidRegion);

  // Compositor thread. Copies the painted region to the front bitmap and
  // asks the BWindow (through its messenger, never by locking it) to redraw
  // that region from the front bitmap.
  void EndRemoteDrawingInRegion(
      mozilla::gfx::DrawTarget* aDrawTarget,
      const LayoutDeviceIntRegion& aInvalidRegion);

  // Main thread: where to send redraw requests (invalid messenger = none).
  void SetTarget(const BMessenger& aTarget);

  void DrawToView(BView* aView, BRect aRect);

  void Destroy();
  bool HasContent();

 private:
  mozilla::Mutex mRenderingLock;
  BBitmap* mBackBitmap;
  BBitmap* mFrontBitmap;
  BMessenger mTarget;  // guarded by mRenderingLock
};

}  // namespace mozilla::widget

#endif  // mozilla_widget_HaikuWindowBuffer_h
