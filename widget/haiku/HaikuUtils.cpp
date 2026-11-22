/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuUtils.h"
#include "HaikuApplication.h"
#include "imgIContainer.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/RefPtr.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace mozilla::gfx;

namespace mozilla::widget {

void EnsureHaikuApplication() {
  HaikuApplication::EnsureInitialized();
}

// Haiku scales its UI from the plain font size: 12 pt is 1x. Round to a
// quarter so pixels stay crisp. layout.css.devPixelsPerPx still overrides
// this (nsBaseWidget::GetDefaultScale checks it first).
double GetHaikuSystemScale() {
  const char* forceScale = getenv("MOZ_HAIKU_SCALE");
  if (forceScale) {
    double val = atof(forceScale);
    if (val > 0.0) {
      return val;
    }
  }
  if (be_plain_font && be_plain_font->Size() > 0.0f) {
    double scale = std::round(be_plain_font->Size() / 12.0 * 4.0) / 4.0;
    return std::max(1.0, scale);
  }
  return 1.0;
}

uint32_t GetHaikuPanelColorPixel() {
  rgb_color c = ui_color(B_PANEL_BACKGROUND_COLOR);
  return (uint32_t(255) << 24) |
         (uint32_t(c.red) << 16) |
         (uint32_t(c.green) << 8) |
         uint32_t(c.blue);
}

BBitmap* CreateBBitmapFromSurface(
    SourceSurface* aSurface,
    float aAlphaMultiplier,
    int32_t aMaxWidth,
    int32_t aMaxHeight) {
  if (!be_app || !aSurface) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> dataSurface = aSurface->GetDataSurface();
  if (!dataSurface) {
    return nullptr;
  }

  IntSize size = dataSurface->GetSize();
  if (size.width <= 0 || size.height <= 0) {
    return nullptr;
  }

  DataSourceSurface::MappedSurface map;
  if (!dataSurface->Map(DataSourceSurface::MapType::READ, &map)) {
    return nullptr;
  }

  int32_t dstWidth = size.width;
  int32_t dstHeight = size.height;

  bool needScale = (aMaxWidth > 0 && aMaxHeight > 0) &&
                   (dstWidth > aMaxWidth || dstHeight > aMaxHeight);

  if (needScale) {
    double ratio = std::min(double(aMaxWidth) / dstWidth,
                            double(aMaxHeight) / dstHeight);
    dstWidth = std::max(1, int32_t(std::round(dstWidth * ratio)));
    dstHeight = std::max(1, int32_t(std::round(dstHeight * ratio)));
  }

  BRect bounds(0, 0, dstWidth - 1, dstHeight - 1);
  auto* bitmap = new BBitmap(bounds, B_RGBA32);
  if (bitmap->InitCheck() != B_OK || !bitmap->Bits()) {
    delete bitmap;
    dataSurface->Unmap();
    return nullptr;
  }

  uint8_t* dstBits = static_cast<uint8_t*>(bitmap->Bits());
  int32_t dstStride = bitmap->BytesPerRow();
  const uint8_t* srcBits = map.mData;
  int32_t srcStride = map.mStride;

  const bool applyAlpha = (aAlphaMultiplier >= 0.0f && aAlphaMultiplier < 1.0f);

  auto processPixel = [&](const uint8_t* aSrc, uint8_t* aDst) {
    if (applyAlpha) {
      aDst[0] = static_cast<uint8_t>(aSrc[0] * aAlphaMultiplier);
      aDst[1] = static_cast<uint8_t>(aSrc[1] * aAlphaMultiplier);
      aDst[2] = static_cast<uint8_t>(aSrc[2] * aAlphaMultiplier);
      aDst[3] = static_cast<uint8_t>(aSrc[3] * aAlphaMultiplier);
    } else {
      memcpy(aDst, aSrc, 4);
    }
  };

  if (needScale) {
    for (int32_t y = 0; y < dstHeight; ++y) {
      int32_t srcY = (y * size.height) / dstHeight;
      const uint8_t* srcRow = srcBits + srcY * srcStride;
      uint8_t* dstRow = dstBits + y * dstStride;
      for (int32_t x = 0; x < dstWidth; ++x) {
        int32_t srcX = (x * size.width) / dstWidth;
        processPixel(srcRow + srcX * 4, dstRow + x * 4);
      }
    }
  } else if (applyAlpha) {
    for (int32_t y = 0; y < size.height; ++y) {
      const uint8_t* srcRow = srcBits + y * srcStride;
      uint8_t* dstRow = dstBits + y * dstStride;
      for (int32_t x = 0; x < size.width; ++x) {
        processPixel(srcRow + x * 4, dstRow + x * 4);
      }
    }
  } else {
    int32_t rowBytes = size.width * 4;
    for (int32_t y = 0; y < size.height; ++y) {
      memcpy(dstBits + y * dstStride, srcBits + y * srcStride, rowBytes);
    }
  }

  dataSurface->Unmap();
  return bitmap;
}

bool EncodeBitmapAsPNG(BBitmap* aBitmap, BMallocIO& aOut) {
  BBitmapStream stream(aBitmap);  // takes ownership
  bool ok = BTranslatorRoster::Default()->Translate(
                &stream, nullptr, nullptr, &aOut, 'PNG ') == B_OK;
  BBitmap* detached = nullptr;
  stream.DetachBitmap(&detached);
  delete detached;
  return ok && aOut.BufferLength() > 0;
}

BBitmap* CreateBBitmapFromImage(
    imgIContainer* aImage,
    float aAlphaMultiplier,
    int32_t aMaxWidth,
    int32_t aMaxHeight) {
  if (!be_app || !aImage) {
    return nullptr;
  }
  RefPtr<SourceSurface> surface = aImage->GetFrame(
      imgIContainer::FRAME_CURRENT,
      imgIContainer::FLAG_SYNC_DECODE);
  if (!surface) {
    return nullptr;
  }
  return CreateBBitmapFromSurface(surface, aAlphaMultiplier, aMaxWidth, aMaxHeight);
}

}  // namespace mozilla::widget
