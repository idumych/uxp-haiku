/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuWindowBuffer.h"
#include "HaikuView.h"
#include "nsWindow.h"

#include <algorithm>
#include <cstring>

using namespace mozilla::gfx;

namespace mozilla::widget {

HaikuWindowBuffer::HaikuWindowBuffer()
    : mRenderingLock("HaikuWindowBuffer::mRenderingLock"),
      mBackBitmap(nullptr),
      mFrontBitmap(nullptr) {}

HaikuWindowBuffer::~HaikuWindowBuffer() {
  Destroy();
}

void HaikuWindowBuffer::Destroy() {
  MutexAutoLock lock(mRenderingLock);
  delete mBackBitmap;
  mBackBitmap = nullptr;
  delete mFrontBitmap;
  mFrontBitmap = nullptr;
}

already_AddRefed<DrawTarget> HaikuWindowBuffer::StartRemoteDrawingInRegion(
    const LayoutDeviceIntSize& aClientSize,
    const LayoutDeviceIntRegion& aInvalidRegion) {
  MutexAutoLock lock(mRenderingLock);

  int32_t width = aClientSize.width;
  int32_t height = aClientSize.height;

  if (width <= 0 || height <= 0) {
    return nullptr;
  }

  if (!mBackBitmap ||
      int32_t(mBackBitmap->Bounds().Width()) + 1 != width ||
      int32_t(mBackBitmap->Bounds().Height()) + 1 != height) {

    BBitmap* oldBitmap = mBackBitmap;
    mBackBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), 0, B_RGBA32);

    if (!mBackBitmap || mBackBitmap->InitCheck() != B_OK || !mBackBitmap->Bits()) {
      delete mBackBitmap;
      mBackBitmap = nullptr;
      delete oldBitmap;
      return nullptr;
    }

    uint32_t bgPixel = GetHaikuPanelColorPixel();
    uint8_t* dstBits = static_cast<uint8_t*>(mBackBitmap->Bits());
    int32_t dstStride = mBackBitmap->BytesPerRow();

    for (int32_t y = 0; y < height; ++y) {
      uint32_t* row = reinterpret_cast<uint32_t*>(dstBits + y * dstStride);
      std::fill_n(row, width, bgPixel);
    }

    BBitmap* srcBitmap = mFrontBitmap ? mFrontBitmap : oldBitmap;
    if (srcBitmap && srcBitmap->IsValid() && srcBitmap->Bits()) {
      int32_t srcWidth = int32_t(srcBitmap->Bounds().Width()) + 1;
      int32_t srcHeight = int32_t(srcBitmap->Bounds().Height()) + 1;

      int32_t copyW = std::min(width, srcWidth);
      int32_t copyH = std::min(height, srcHeight);

      uint8_t* srcBits = static_cast<uint8_t*>(srcBitmap->Bits());
      int32_t srcStride = srcBitmap->BytesPerRow();

      for (int32_t y = 0; y < copyH; ++y) {
        memcpy(dstBits + y * dstStride, srcBits + y * srcStride, copyW * 4);
      }
    }

    delete oldBitmap;
  }

  IntSize size(width, height);
  return Factory::CreateDrawTargetForData(
      BackendType::CAIRO,
      static_cast<uint8_t*>(mBackBitmap->Bits()),
      size,
      mBackBitmap->BytesPerRow(),
      SurfaceFormat::B8G8R8A8);
}

void HaikuWindowBuffer::EndRemoteDrawingInRegion(
    nsWindow* aGeckoWindow,
    HaikuWindow* aWindow,
    HaikuView* aView,
    DrawTarget* aDrawTarget,
    const LayoutDeviceIntRegion& aInvalidRegion) {
  int32_t curWidth = 0;
  int32_t curHeight = 0;
  LayoutDeviceIntRect dirtyRect;

  {
    MutexAutoLock lock(mRenderingLock);
    if (!mBackBitmap) {
      return;
    }

    curWidth = int32_t(mBackBitmap->Bounds().Width()) + 1;
    curHeight = int32_t(mBackBitmap->Bounds().Height()) + 1;

    if (!mFrontBitmap || mFrontBitmap->Bounds() != mBackBitmap->Bounds()) {
      delete mFrontBitmap;
      mFrontBitmap = new BBitmap(mBackBitmap->Bounds(), 0, B_RGBA32);
      if (mFrontBitmap && mFrontBitmap->IsValid()) {
        memcpy(mFrontBitmap->Bits(), mBackBitmap->Bits(), mBackBitmap->BitsLength());
      }
      dirtyRect = LayoutDeviceIntRect(0, 0, curWidth, curHeight);
    } else {
      dirtyRect = aInvalidRegion.GetBounds();
      BRect bounds = mBackBitmap->Bounds();
      int32_t bmpWidth = int32_t(bounds.Width()) + 1;
      int32_t bmpHeight = int32_t(bounds.Height()) + 1;

      int32_t startY = std::max(0, dirtyRect.y);
      int32_t endY = std::min(bmpHeight, dirtyRect.YMost());
      int32_t startX = std::max(0, dirtyRect.x);
      int32_t copyWidth = std::min(bmpWidth - startX, dirtyRect.width);

      if (copyWidth > 0 && startY < endY) {
        uint8_t* dst = static_cast<uint8_t*>(mFrontBitmap->Bits());
        uint8_t* src = static_cast<uint8_t*>(mBackBitmap->Bits());
        int32_t stride = mBackBitmap->BytesPerRow();

        for (int32_t y = startY; y < endY; ++y) {
          memcpy(dst + y * stride + startX * 4,
                 src + y * stride + startX * 4,
                 copyWidth * 4);
        }
      }
    }
  }

  if (dirtyRect.IsEmpty()) {
    return;
  }

  if (aWindow && aView) {
    if (aWindow->LockWithTimeout(15000) == B_OK) {
      BRect r(dirtyRect.x, dirtyRect.y,
              dirtyRect.XMost() - 1, dirtyRect.YMost() - 1);
      {
        MutexAutoLock lock(mRenderingLock);
        if (mFrontBitmap && mFrontBitmap->IsValid()) {
          BRect bmpBounds = mFrontBitmap->Bounds();
          BRect srcRect = r & bmpBounds;
          if (srcRect.IsValid()) {
            aView->DrawBitmap(mFrontBitmap, srcRect, srcRect);
            aView->Sync();
          }
        }
      }
      aWindow->Unlock();
      return;
    }
  }

  // Couldn't lock the BWindow in time: let it redraw from the front buffer.
  if (aWindow) {
    BMessage msg(kMsgGeckoInvalidate);
    msg.AddRect("rect", BRect(dirtyRect.x, dirtyRect.y, dirtyRect.XMost() - 1,
                              dirtyRect.YMost() - 1));
    aWindow->PostMessage(&msg);
  }
}

bool HaikuWindowBuffer::HasContent() {
  MutexAutoLock lock(mRenderingLock);
  return mFrontBitmap != nullptr;
}

void HaikuWindowBuffer::DrawToView(BView* aView, BRect aRect) {
  MutexAutoLock lock(mRenderingLock);
  if (!aView) {
    return;
  }

  rgb_color bgColor = ui_color(B_PANEL_BACKGROUND_COLOR);

  if (mFrontBitmap && mFrontBitmap->IsValid()) {
    BRect bmpBounds = mFrontBitmap->Bounds();
    BRect srcRect = aRect & bmpBounds;
    if (srcRect.IsValid()) {
      aView->DrawBitmap(mFrontBitmap, srcRect, srcRect);
    }
  } else {
    aView->SetHighColor(bgColor);
    aView->FillRect(aRect);
  }
}

}  // namespace mozilla::widget
