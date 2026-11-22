/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeThemeHaiku.h"

#include <algorithm>
#include <vector>

#include "gfxContext.h"
#include "mozilla/EventStates.h"
#include "mozilla/gfx/2D.h"
#include "nsAttrValueInlines.h"
#include "nsDeviceContext.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsNameSpaceManager.h"
#include "nsPresContext.h"
#include "nsRenderingContext.h"
#include "nsThemeConstants.h"

#include <interface/Bitmap.h>
#include <interface/ControlLook.h>
#include <interface/InterfaceDefs.h>
#include <interface/ScrollBar.h>
#include <interface/View.h>

using namespace mozilla;
using namespace mozilla::gfx;

namespace {

// Widgets larger than this (in either direction) fall back to the CSS look;
// the offscreen bitmap would get unreasonably large.
const int32_t kMaxThemeBitmapSize = 4096;

// Text fields, menulists and buttons: frame drawn by BControlLook.
const int32_t kTextFieldBorder = 3;
const int32_t kButtonBorderV = 3;
const int32_t kButtonBorderH = 6;
const int32_t kMenuListArrowWidth = 14;

/*
 * One offscreen BBitmap (with a BView attached) that BControlLook draws
 * into. Each widget is drawn twice, over black and over white, which gives
 * premultiplied colour and alpha without relying on how app_server treats
 * the alpha channel of 32-bit bitmaps.
 */
class ThemeCanvas {
 public:
  static ThemeCanvas* Get() {
    if (!sInstance) {
      sInstance = new ThemeCanvas();
    }
    return sInstance;
  }

  static void Shutdown() {
    delete sInstance;
    sInstance = nullptr;
  }

  // aDraw(BView*, BRect, rgb_color background) draws the widget into the
  // rect; aOut receives w*h premultiplied B8G8R8A8 pixels.
  template <typename DrawFunc>
  bool Render(int32_t aWidth, int32_t aHeight, const DrawFunc& aDraw,
              uint8_t* aOut, int32_t aOutStride) {
    if (!Ensure(aWidth, aHeight) || !mBitmap->Lock()) {
      return false;
    }

    const BRect rect(0, 0, aWidth - 1, aHeight - 1);
    const rgb_color backgrounds[2] = {make_color(0, 0, 0, 255),
                                      make_color(255, 255, 255, 255)};
    const uint8_t* bits = static_cast<const uint8_t*>(mBitmap->Bits());
    const int32_t bpr = mBitmap->BytesPerRow();
    mBlack.resize(size_t(aWidth) * aHeight * 4);

    for (int pass = 0; pass < 2; pass++) {
      mView->PushState();
      mView->SetDrawingMode(B_OP_COPY);
      mView->SetHighColor(backgrounds[pass]);
      mView->FillRect(rect);
      mView->SetLowColor(backgrounds[pass]);
      mView->SetHighColor(0, 0, 0);
      aDraw(mView, rect, backgrounds[pass]);
      mView->PopState();
      mView->Sync();

      for (int32_t y = 0; y < aHeight; y++) {
        const uint8_t* src = bits + y * bpr;
        if (pass == 0) {
          memcpy(&mBlack[size_t(y) * aWidth * 4], src, size_t(aWidth) * 4);
          continue;
        }
        const uint8_t* black = &mBlack[size_t(y) * aWidth * 4];
        uint8_t* dst = aOut + y * aOutStride;
        for (int32_t x = 0; x < aWidth; x++, src += 4, black += 4, dst += 4) {
          // B_RGB32 is B, G, R, x in memory, like B8G8R8A8.
          int alpha = 255 - (int(src[1]) - int(black[1]));
          alpha = std::min(255, std::max(0, alpha));
          dst[0] = std::min<int>(black[0], alpha);
          dst[1] = std::min<int>(black[1], alpha);
          dst[2] = std::min<int>(black[2], alpha);
          dst[3] = alpha;
        }
      }
    }

    mBitmap->Unlock();
    return true;
  }

 private:
  ThemeCanvas() : mBitmap(nullptr), mView(nullptr) {}
  ~ThemeCanvas() { delete mBitmap; }  // also deletes mView

  bool Ensure(int32_t aWidth, int32_t aHeight) {
    if (mBitmap && mBitmap->Bounds().IntegerWidth() + 1 >= aWidth &&
        mBitmap->Bounds().IntegerHeight() + 1 >= aHeight) {
      return true;
    }
    int32_t width = aWidth, height = aHeight;
    if (mBitmap) {
      width = std::max(width, mBitmap->Bounds().IntegerWidth() + 1);
      height = std::max(height, mBitmap->Bounds().IntegerHeight() + 1);
      delete mBitmap;
      mBitmap = nullptr;
      mView = nullptr;
    }
    // Round up so that slowly growing widgets don't reallocate every paint.
    width = (width + 63) & ~63;
    height = (height + 31) & ~31;

    BRect bounds(0, 0, width - 1, height - 1);
    mBitmap = new BBitmap(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGB32);
    if (mBitmap->InitCheck() != B_OK) {
      delete mBitmap;
      mBitmap = nullptr;
      return false;
    }
    mView = new BView(bounds, "nsNativeThemeHaiku", B_FOLLOW_NONE, B_WILL_DRAW);
    mBitmap->AddChild(mView);
    return true;
  }

  static ThemeCanvas* sInstance;
  BBitmap* mBitmap;
  BView* mView;
  std::vector<uint8_t> mBlack;
};

ThemeCanvas* ThemeCanvas::sInstance = nullptr;

bool ShouldScrollbarButtonBeDisabled(int32_t aCurpos, int32_t aMaxpos,
                                     uint8_t aWidgetType) {
  return ((aCurpos == 0 && (aWidgetType == NS_THEME_SCROLLBARBUTTON_UP ||
                            aWidgetType == NS_THEME_SCROLLBARBUTTON_LEFT)) ||
          (aCurpos == aMaxpos &&
           (aWidgetType == NS_THEME_SCROLLBARBUTTON_DOWN ||
            aWidgetType == NS_THEME_SCROLLBARBUTTON_RIGHT)));
}

bool IsHorizontalScrollbarPart(uint8_t aWidgetType) {
  switch (aWidgetType) {
    case NS_THEME_SCROLLBAR_HORIZONTAL:
    case NS_THEME_SCROLLBARTRACK_HORIZONTAL:
    case NS_THEME_SCROLLBARTHUMB_HORIZONTAL:
    case NS_THEME_SCROLLBARBUTTON_LEFT:
    case NS_THEME_SCROLLBARBUTTON_RIGHT:
      return true;
  }
  return false;
}

bool IsTextFieldLike(uint8_t aWidgetType) {
  switch (aWidgetType) {
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    case NS_THEME_NUMBER_INPUT:
    case NS_THEME_MENULIST_TEXTFIELD:
    case NS_THEME_LISTBOX:
      return true;
  }
  return false;
}

bool IsFrameContentNodeInNamespace(nsIFrame* aFrame, uint32_t aNamespace) {
  nsIContent* content = aFrame ? aFrame->GetContent() : nullptr;
  return content && content->IsInNamespace(aNamespace);
}

int32_t ScrollbarWidth(uint8_t aWidgetType) {
  float width = be_control_look->GetScrollBarWidth(
      IsHorizontalScrollbarPart(aWidgetType) ? B_HORIZONTAL : B_VERTICAL);
  return std::max(8, int32_t(width + 0.5f));
}

}  // namespace

NS_IMPL_ISUPPORTS_INHERITED(nsNativeThemeHaiku, nsNativeTheme, nsITheme)

nsNativeThemeHaiku::nsNativeThemeHaiku() {}

nsNativeThemeHaiku::~nsNativeThemeHaiku() {}

/* static */ void nsNativeThemeHaiku::Shutdown() { ThemeCanvas::Shutdown(); }

uint32_t nsNativeThemeHaiku::GetControlLookFlags(nsIFrame* aFrame,
                                                 uint8_t aWidgetType) {
  uint32_t flags = 0;
  EventStates state = GetContentState(aFrame, aWidgetType);

  if (IsDisabled(aFrame, state) ||
      (IsTextFieldLike(aWidgetType) && IsReadOnly(aFrame))) {
    flags |= BControlLook::B_DISABLED;
  }
  if (state.HasState(NS_EVENT_STATE_HOVER)) {
    flags |= BControlLook::B_HOVER;
  }
  if (state.HasState(NS_EVENT_STATE_FOCUS)) {
    flags |= BControlLook::B_FOCUSED;
  }
  if (state.HasAllStates(NS_EVENT_STATE_ACTIVE | NS_EVENT_STATE_HOVER)) {
    flags |= BControlLook::B_ACTIVATED;
  }

  switch (aWidgetType) {
    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
      flags &= ~BControlLook::B_ACTIVATED;
      if (aWidgetType == NS_THEME_CHECKBOX && GetIndeterminate(aFrame)) {
        flags |= BControlLook::B_PARTIALLY_ACTIVATED;
      } else if (aWidgetType == NS_THEME_CHECKBOX ? IsChecked(aFrame)
                                                  : IsSelected(aFrame) ||
                                                        IsChecked(aFrame)) {
        flags |= BControlLook::B_ACTIVATED;
      }
      break;
    case NS_THEME_BUTTON:
      if (IsDefaultButton(aFrame)) {
        flags |= BControlLook::B_DEFAULT_BUTTON;
      }
      if (IsCheckedButton(aFrame) || IsOpenButton(aFrame)) {
        flags |= BControlLook::B_ACTIVATED;
      }
      break;
    case NS_THEME_TOOLBARBUTTON:
      if (IsCheckedButton(aFrame) || IsOpenButton(aFrame)) {
        flags |= BControlLook::B_ACTIVATED;
      }
      if (!(flags & (BControlLook::B_HOVER | BControlLook::B_ACTIVATED))) {
        flags |= BControlLook::B_FLAT;
      }
      break;
    case NS_THEME_MENULIST:
      if (IsOpenButton(aFrame)) {
        flags |= BControlLook::B_ACTIVATED;
      }
      break;
    case NS_THEME_SCROLLBARTHUMB_HORIZONTAL:
    case NS_THEME_SCROLLBARTHUMB_VERTICAL:
      if (CheckBooleanAttr(aFrame, nsGkAtoms::active)) {
        flags |= BControlLook::B_ACTIVATED;
      }
      break;
    case NS_THEME_SCROLLBARBUTTON_UP:
    case NS_THEME_SCROLLBARBUTTON_DOWN:
    case NS_THEME_SCROLLBARBUTTON_LEFT:
    case NS_THEME_SCROLLBARBUTTON_RIGHT: {
      int32_t curpos = CheckIntAttr(aFrame, nsGkAtoms::curpos, 0);
      int32_t maxpos = CheckIntAttr(aFrame, nsGkAtoms::maxpos, 100);
      if (ShouldScrollbarButtonBeDisabled(curpos, maxpos, aWidgetType)) {
        flags |= BControlLook::B_DISABLED;
      } else if (CheckBooleanAttr(aFrame, nsGkAtoms::active)) {
        flags |= BControlLook::B_ACTIVATED;
      }
      break;
    }
  }
  return flags;
}

NS_IMETHODIMP
nsNativeThemeHaiku::DrawWidgetBackground(nsRenderingContext* aContext,
                                         nsIFrame* aFrame,
                                         uint8_t aWidgetType,
                                         const nsRect& aRect,
                                         const nsRect& aDirtyRect) {
  if (!be_control_look) {
    return NS_OK;
  }

  gfxContext* ctx = aContext->ThebesContext();
  DrawTarget* dt = aContext->GetDrawTarget();
  nsPresContext* presContext = aFrame->PresContext();

  gfxRect rect = presContext->AppUnitsToGfxUnits(aRect);
  bool snapped = ctx->UserToDevicePixelSnapped(rect);
  if (!snapped) {
    rect.Round();
  }

  const int32_t width = NSToIntRound(rect.Width());
  const int32_t height = NSToIntRound(rect.Height());
  if (width <= 0 || height <= 0 || width > kMaxThemeBitmapSize ||
      height > kMaxThemeBitmapSize) {
    return NS_OK;
  }

  const uint32_t flags = GetControlLookFlags(aFrame, aWidgetType);
  const rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
  const rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
  const orientation orient =
      IsHorizontalScrollbarPart(aWidgetType) ? B_HORIZONTAL : B_VERTICAL;

  double progress = 0.0;
  if (aWidgetType == NS_THEME_PROGRESSBAR) {
    EventStates state = GetContentState(aFrame, aWidgetType);
    double maxValue = GetProgressMaxValue(aFrame);
    if (IsIndeterminateProgress(aFrame, state)) {
      progress = 1.0;
    } else if (maxValue > 0) {
      progress = std::min(1.0, std::max(0.0, GetProgressValue(aFrame) / maxValue));
    }
  }
  const bool bottomTab = aWidgetType == NS_THEME_TAB && IsBottomTab(aFrame);
  const bool selectedTab = aWidgetType == NS_THEME_TAB && IsSelectedTab(aFrame);
  scroll_bar_info scrollInfo;
  if (get_scroll_bar_info(&scrollInfo) != B_OK) {
    scrollInfo.knob = BControlLook::B_KNOB_NONE;
  }

  auto draw = [&](BView* aView, BRect aBounds, const rgb_color& aBackground) {
    BRect r = aBounds;
    switch (aWidgetType) {
      case NS_THEME_BUTTON:
      case NS_THEME_TOOLBARBUTTON:
        if (!(flags & BControlLook::B_FLAT)) {
          be_control_look->DrawButtonFrame(aView, r, aBounds, base,
                                           aBackground, flags);
        }
        be_control_look->DrawButtonBackground(aView, r, aBounds, base, flags);
        break;

      case NS_THEME_CHECKBOX:
        be_control_look->DrawCheckBox(aView, r, aBounds, base, flags);
        break;

      case NS_THEME_RADIO:
        be_control_look->DrawRadioButton(aView, r, aBounds, base, flags);
        break;

      case NS_THEME_TEXTFIELD:
      case NS_THEME_TEXTFIELD_MULTILINE:
      case NS_THEME_SEARCHFIELD:
      case NS_THEME_NUMBER_INPUT:
      case NS_THEME_MENULIST_TEXTFIELD:
      case NS_THEME_LISTBOX:
        be_control_look->DrawTextControlBorder(aView, r, aBounds, base, flags);
        aView->SetHighColor(flags & BControlLook::B_DISABLED
                                ? tint_color(document, B_DARKEN_1_TINT)
                                : document);
        aView->FillRect(r);
        break;

      case NS_THEME_MENULIST:
        be_control_look->DrawMenuFieldFrame(aView, r, aBounds, base,
                                            aBackground, flags);
        be_control_look->DrawMenuFieldBackground(aView, r, aBounds, base,
                                                 true, flags);
        break;

      case NS_THEME_MENULIST_BUTTON:
        be_control_look->DrawArrowShape(aView, r, aBounds, base,
                                        BControlLook::B_DOWN_ARROW, flags);
        break;

      case NS_THEME_PROGRESSBAR:
        be_control_look->DrawStatusBar(
            aView, r, aBounds, base, ui_color(B_STATUS_BAR_COLOR),
            aBounds.left + (aBounds.Width() + 1) * progress);
        break;

      case NS_THEME_PROGRESSCHUNK:
        // PROGRESSBAR draws the whole bar, value included.
        break;

      case NS_THEME_SCROLLBAR_HORIZONTAL:
      case NS_THEME_SCROLLBAR_VERTICAL:
        be_control_look->DrawScrollBarBorder(aView, r, aBounds, base, flags,
                                             orient);
        r.InsetBy(1, 1);
        be_control_look->DrawScrollBarBackground(aView, r, aBounds, base,
                                                 flags, orient);
        break;

      case NS_THEME_SCROLLBARTRACK_HORIZONTAL:
      case NS_THEME_SCROLLBARTRACK_VERTICAL:
        // Drawn with the scrollbar.
        break;

      case NS_THEME_SCROLLBARTHUMB_HORIZONTAL:
      case NS_THEME_SCROLLBARTHUMB_VERTICAL:
        be_control_look->DrawScrollBarThumb(aView, r, aBounds, base, flags,
                                            orient, scrollInfo.knob);
        break;

      case NS_THEME_SCROLLBARBUTTON_UP:
      case NS_THEME_SCROLLBARBUTTON_DOWN:
      case NS_THEME_SCROLLBARBUTTON_LEFT:
      case NS_THEME_SCROLLBARBUTTON_RIGHT: {
        int32 direction = aWidgetType == NS_THEME_SCROLLBARBUTTON_UP
                              ? BControlLook::B_UP_ARROW
                          : aWidgetType == NS_THEME_SCROLLBARBUTTON_DOWN
                              ? BControlLook::B_DOWN_ARROW
                          : aWidgetType == NS_THEME_SCROLLBARBUTTON_LEFT
                              ? BControlLook::B_LEFT_ARROW
                              : BControlLook::B_RIGHT_ARROW;
        be_control_look->DrawScrollBarButton(
            aView, r, aBounds, base, ui_color(B_PANEL_TEXT_COLOR), flags,
            direction, orient, (flags & BControlLook::B_ACTIVATED) != 0);
        break;
      }

      case NS_THEME_TAB: {
        uint32 side = bottomTab ? BControlLook::B_BOTTOM_BORDER
                                : BControlLook::B_TOP_BORDER;
        if (selectedTab) {
          be_control_look->DrawActiveTab(aView, r, aBounds, base, flags,
                                         BControlLook::B_ALL_BORDERS, side);
        } else {
          be_control_look->DrawInactiveTab(aView, r, aBounds, base, flags,
                                           BControlLook::B_ALL_BORDERS, side);
        }
        break;
      }

      case NS_THEME_TABPANELS:
        be_control_look->DrawBorder(aView, r, aBounds, base, B_FANCY_BORDER,
                                    flags);
        aView->SetHighColor(base);
        aView->FillRect(r);
        break;

      case NS_THEME_TOOLBOX:
      case NS_THEME_TOOLBAR:
      case NS_THEME_STATUSBAR:
        aView->SetHighColor(base);
        aView->FillRect(r);
        break;
    }
  };

  RefPtr<DataSourceSurface> surface = Factory::CreateDataSourceSurface(
      IntSize(width, height), SurfaceFormat::B8G8R8A8);
  if (!surface) {
    return NS_OK;
  }
  {
    DataSourceSurface::ScopedMap map(surface, DataSourceSurface::WRITE);
    if (!map.IsMapped() ||
        !ThemeCanvas::Get()->Render(width, height, draw, map.GetData(),
                                    map.GetStride())) {
      return NS_OK;
    }
  }

  Rect dest(rect.X(), rect.Y(), width, height);
  Rect source(0, 0, width, height);
  if (snapped) {
    // rect is in device pixels; draw without the user-space transform.
    Matrix oldTransform = dt->GetTransform();
    dt->SetTransform(Matrix());
    dt->DrawSurface(surface, dest, source);
    dt->SetTransform(oldTransform);
  } else {
    dt->DrawSurface(surface, dest, source);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeHaiku::GetWidgetBorder(nsDeviceContext* aContext,
                                    nsIFrame* aFrame, uint8_t aWidgetType,
                                    nsIntMargin* aResult) {
  aResult->SizeTo(0, 0, 0, 0);
  switch (aWidgetType) {
    case NS_THEME_BUTTON:
    case NS_THEME_TOOLBARBUTTON:
      aResult->SizeTo(kButtonBorderV, kButtonBorderH, kButtonBorderV,
                      kButtonBorderH);
      break;
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    case NS_THEME_NUMBER_INPUT:
    case NS_THEME_MENULIST_TEXTFIELD:
    case NS_THEME_LISTBOX:
      aResult->SizeTo(kTextFieldBorder, kTextFieldBorder, kTextFieldBorder,
                      kTextFieldBorder);
      break;
    case NS_THEME_MENULIST: {
      // Leave room for the popup indicator that DrawMenuFieldBackground
      // draws at the trailing edge.
      int32_t arrow = kButtonBorderH + kMenuListArrowWidth;
      if (IsFrameRTL(aFrame)) {
        aResult->SizeTo(kButtonBorderV, kButtonBorderH, kButtonBorderV, arrow);
      } else {
        aResult->SizeTo(kButtonBorderV, arrow, kButtonBorderV, kButtonBorderH);
      }
      break;
    }
    case NS_THEME_SCROLLBAR_HORIZONTAL:
    case NS_THEME_SCROLLBAR_VERTICAL:
      aResult->SizeTo(1, 1, 1, 1);
      break;
    case NS_THEME_TABPANELS:
      aResult->SizeTo(2, 2, 2, 2);
      break;
  }
  return NS_OK;
}

bool nsNativeThemeHaiku::GetWidgetPadding(nsDeviceContext* aContext,
                                          nsIFrame* aFrame,
                                          uint8_t aWidgetType,
                                          nsIntMargin* aResult) {
  switch (aWidgetType) {
    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
      aResult->SizeTo(0, 0, 0, 0);
      return true;
  }
  return false;
}

bool nsNativeThemeHaiku::GetWidgetOverflow(nsDeviceContext* aContext,
                                           nsIFrame* aFrame,
                                           uint8_t aWidgetType,
                                           nsRect* aOverflowRect) {
  return false;
}

NS_IMETHODIMP
nsNativeThemeHaiku::GetMinimumWidgetSize(nsPresContext* aPresContext,
                                         nsIFrame* aFrame,
                                         uint8_t aWidgetType,
                                         LayoutDeviceIntSize* aResult,
                                         bool* aIsOverridable) {
  aResult->width = aResult->height = 0;
  *aIsOverridable = true;

  switch (aWidgetType) {
    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
      aResult->width = aResult->height = 13;
      *aIsOverridable = false;
      break;

    case NS_THEME_SCROLLBAR_VERTICAL: {
      int32_t w = ScrollbarWidth(aWidgetType);
      aResult->width = w;
      aResult->height = 3 * w;
      *aIsOverridable = false;
      break;
    }
    case NS_THEME_SCROLLBAR_HORIZONTAL: {
      int32_t w = ScrollbarWidth(aWidgetType);
      aResult->width = 3 * w;
      aResult->height = w;
      *aIsOverridable = false;
      break;
    }
    case NS_THEME_SCROLLBARBUTTON_UP:
    case NS_THEME_SCROLLBARBUTTON_DOWN:
    case NS_THEME_SCROLLBARBUTTON_LEFT:
    case NS_THEME_SCROLLBARBUTTON_RIGHT: {
      // Inside the scrollbar's 1px border.
      int32_t w = ScrollbarWidth(aWidgetType) - 2;
      aResult->width = aResult->height = w;
      *aIsOverridable = false;
      break;
    }
    case NS_THEME_SCROLLBARTHUMB_VERTICAL: {
      int32_t w = ScrollbarWidth(aWidgetType) - 2;
      aResult->width = w;
      aResult->height = w;
      *aIsOverridable = false;
      break;
    }
    case NS_THEME_SCROLLBARTHUMB_HORIZONTAL: {
      int32_t w = ScrollbarWidth(aWidgetType) - 2;
      aResult->width = w;
      aResult->height = w;
      *aIsOverridable = false;
      break;
    }
    case NS_THEME_SCROLLBARTRACK_VERTICAL:
      aResult->width = ScrollbarWidth(aWidgetType) - 2;
      break;
    case NS_THEME_SCROLLBARTRACK_HORIZONTAL:
      aResult->height = ScrollbarWidth(aWidgetType) - 2;
      break;

    case NS_THEME_MENULIST_BUTTON:
      aResult->width = kMenuListArrowWidth;
      break;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeHaiku::WidgetStateChanged(nsIFrame* aFrame, uint8_t aWidgetType,
                                       nsIAtom* aAttribute,
                                       bool* aShouldRepaint,
                                       const nsAttrValue* aOldValue) {
  // Some widget types just never change state.
  if (aWidgetType == NS_THEME_TOOLBOX || aWidgetType == NS_THEME_TOOLBAR ||
      aWidgetType == NS_THEME_STATUSBAR ||
      aWidgetType == NS_THEME_PROGRESSCHUNK ||
      aWidgetType == NS_THEME_TABPANELS) {
    *aShouldRepaint = false;
    return NS_OK;
  }

  // The bar shows the value, so repaint when it changes.
  if (aWidgetType == NS_THEME_PROGRESSBAR) {
    *aShouldRepaint = !aAttribute || aAttribute == nsGkAtoms::value ||
                      aAttribute == nsGkAtoms::max;
    return NS_OK;
  }

  if ((aWidgetType == NS_THEME_SCROLLBARTHUMB_VERTICAL ||
       aWidgetType == NS_THEME_SCROLLBARTHUMB_HORIZONTAL) &&
      aAttribute == nsGkAtoms::active) {
    *aShouldRepaint = true;
    return NS_OK;
  }

  if ((aWidgetType == NS_THEME_SCROLLBARBUTTON_UP ||
       aWidgetType == NS_THEME_SCROLLBARBUTTON_DOWN ||
       aWidgetType == NS_THEME_SCROLLBARBUTTON_LEFT ||
       aWidgetType == NS_THEME_SCROLLBARBUTTON_RIGHT) &&
      (aAttribute == nsGkAtoms::curpos || aAttribute == nsGkAtoms::maxpos)) {
    if (aAttribute == nsGkAtoms::curpos && aOldValue) {
      int32_t curpos = CheckIntAttr(aFrame, nsGkAtoms::curpos, 0);
      int32_t maxpos = CheckIntAttr(aFrame, nsGkAtoms::maxpos, 0);
      nsAutoString str;
      aOldValue->ToString(str);
      nsresult err;
      int32_t oldCurpos = str.ToInteger(&err);
      if (str.IsEmpty() || NS_FAILED(err)) {
        *aShouldRepaint = true;
      } else {
        *aShouldRepaint =
            ShouldScrollbarButtonBeDisabled(oldCurpos, maxpos, aWidgetType) !=
            ShouldScrollbarButtonBeDisabled(curpos, maxpos, aWidgetType);
      }
    } else {
      *aShouldRepaint = true;
    }
    return NS_OK;
  }

  if (!aAttribute) {
    // Hover/focus/active changed.
    *aShouldRepaint = true;
  } else {
    *aShouldRepaint = aAttribute == nsGkAtoms::disabled ||
                      aAttribute == nsGkAtoms::checked ||
                      aAttribute == nsGkAtoms::selected ||
                      aAttribute == nsGkAtoms::visuallyselected ||
                      aAttribute == nsGkAtoms::focused ||
                      aAttribute == nsGkAtoms::readonly ||
                      aAttribute == nsGkAtoms::_default ||
                      aAttribute == nsGkAtoms::menuactive ||
                      aAttribute == nsGkAtoms::open ||
                      aAttribute == nsGkAtoms::parentfocused;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeHaiku::ThemeChanged() { return NS_OK; }

NS_IMETHODIMP_(bool)
nsNativeThemeHaiku::ThemeSupportsWidget(nsPresContext* aPresContext,
                                        nsIFrame* aFrame,
                                        uint8_t aWidgetType) {
  switch (aWidgetType) {
    case NS_THEME_MENULIST:
    case NS_THEME_MENULIST_TEXTFIELD:
      // No native look for vertical writing modes.
      if (aFrame && aFrame->GetWritingMode().IsVertical()) {
        return false;
      }
      return !IsWidgetStyled(aPresContext, aFrame, aWidgetType);

    case NS_THEME_MENULIST_BUTTON:
      // Like GTK: native dropdown buttons only in XUL.
      if (aFrame && aFrame->GetWritingMode().IsVertical()) {
        return false;
      }
      return (!aFrame ||
              IsFrameContentNodeInNamespace(aFrame, kNameSpaceID_XUL)) &&
             !IsWidgetStyled(aPresContext, aFrame, aWidgetType);

    case NS_THEME_BUTTON:
    case NS_THEME_TOOLBARBUTTON:
    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    case NS_THEME_NUMBER_INPUT:
    case NS_THEME_LISTBOX:
    case NS_THEME_PROGRESSBAR:
    case NS_THEME_PROGRESSCHUNK:
    case NS_THEME_TAB:
    case NS_THEME_TABPANELS:
    case NS_THEME_TOOLBOX:
    case NS_THEME_TOOLBAR:
    case NS_THEME_STATUSBAR:
      return !IsWidgetStyled(aPresContext, aFrame, aWidgetType);

    case NS_THEME_SCROLLBAR_HORIZONTAL:
    case NS_THEME_SCROLLBAR_VERTICAL:
    case NS_THEME_SCROLLBARTRACK_HORIZONTAL:
    case NS_THEME_SCROLLBARTRACK_VERTICAL:
    case NS_THEME_SCROLLBARTHUMB_HORIZONTAL:
    case NS_THEME_SCROLLBARTHUMB_VERTICAL:
    case NS_THEME_SCROLLBARBUTTON_UP:
    case NS_THEME_SCROLLBARBUTTON_DOWN:
    case NS_THEME_SCROLLBARBUTTON_LEFT:
    case NS_THEME_SCROLLBARBUTTON_RIGHT:
      return true;
  }
  return false;
}

NS_IMETHODIMP_(bool)
nsNativeThemeHaiku::WidgetIsContainer(uint8_t aWidgetType) {
  switch (aWidgetType) {
    case NS_THEME_MENULIST_BUTTON:
    case NS_THEME_RADIO:
    case NS_THEME_CHECKBOX:
    case NS_THEME_PROGRESSCHUNK:
    case NS_THEME_SCROLLBARBUTTON_UP:
    case NS_THEME_SCROLLBARBUTTON_DOWN:
    case NS_THEME_SCROLLBARBUTTON_LEFT:
    case NS_THEME_SCROLLBARBUTTON_RIGHT:
      return false;
  }
  return true;
}

NS_IMETHODIMP_(bool)
nsNativeThemeHaiku::ThemeDrawsFocusForWidget(uint8_t aWidgetType) {
  switch (aWidgetType) {
    case NS_THEME_BUTTON:
    case NS_THEME_MENULIST:
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    case NS_THEME_NUMBER_INPUT:
      return true;
  }
  return false;
}

bool nsNativeThemeHaiku::ThemeNeedsComboboxDropmarker() { return false; }

nsITheme::Transparency nsNativeThemeHaiku::GetWidgetTransparency(
    nsIFrame* aFrame, uint8_t aWidgetType) {
  switch (aWidgetType) {
    case NS_THEME_TOOLBOX:
    case NS_THEME_TOOLBAR:
    case NS_THEME_STATUSBAR:
    case NS_THEME_SCROLLBAR_HORIZONTAL:
    case NS_THEME_SCROLLBAR_VERTICAL:
      return eOpaque;
  }
  return eUnknownTransparency;
}
