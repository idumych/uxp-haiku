/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNativeThemeHaiku_h_
#define nsNativeThemeHaiku_h_

#include "nsITheme.h"
#include "nsNativeTheme.h"

/*
 * Native theme for Haiku: form controls, scrollbars, tabs and toolbars are
 * drawn with the system BControlLook (be_control_look) into an offscreen
 * BBitmap and the pixels are composited onto the Moz2D DrawTarget.
 */
class nsNativeThemeHaiku final : private nsNativeTheme, public nsITheme {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  NS_IMETHOD DrawWidgetBackground(nsRenderingContext* aContext,
                                  nsIFrame* aFrame, uint8_t aWidgetType,
                                  const nsRect& aRect,
                                  const nsRect& aDirtyRect) override;

  NS_IMETHOD GetWidgetBorder(nsDeviceContext* aContext, nsIFrame* aFrame,
                             uint8_t aWidgetType,
                             nsIntMargin* aResult) override;

  bool GetWidgetPadding(nsDeviceContext* aContext, nsIFrame* aFrame,
                        uint8_t aWidgetType, nsIntMargin* aResult) override;

  bool GetWidgetOverflow(nsDeviceContext* aContext, nsIFrame* aFrame,
                         uint8_t aWidgetType, nsRect* aOverflowRect) override;

  NS_IMETHOD GetMinimumWidgetSize(nsPresContext* aPresContext,
                                  nsIFrame* aFrame, uint8_t aWidgetType,
                                  mozilla::LayoutDeviceIntSize* aResult,
                                  bool* aIsOverridable) override;

  NS_IMETHOD WidgetStateChanged(nsIFrame* aFrame, uint8_t aWidgetType,
                                nsIAtom* aAttribute, bool* aShouldRepaint,
                                const nsAttrValue* aOldValue) override;

  NS_IMETHOD ThemeChanged() override;

  NS_IMETHOD_(bool) ThemeSupportsWidget(nsPresContext* aPresContext,
                                        nsIFrame* aFrame,
                                        uint8_t aWidgetType) override;

  NS_IMETHOD_(bool) WidgetIsContainer(uint8_t aWidgetType) override;

  NS_IMETHOD_(bool) ThemeDrawsFocusForWidget(uint8_t aWidgetType) override;

  bool ThemeNeedsComboboxDropmarker() override;

  Transparency GetWidgetTransparency(nsIFrame* aFrame,
                                     uint8_t aWidgetType) override;

  nsNativeThemeHaiku();

  // Frees the offscreen bitmap; called from the widget module destructor.
  static void Shutdown();

 private:
  ~nsNativeThemeHaiku() override;

  // Collects BControlLook flags (B_DISABLED, B_HOVER, ...) for the frame.
  uint32_t GetControlLookFlags(nsIFrame* aFrame, uint8_t aWidgetType);
};

#endif  // nsNativeThemeHaiku_h_
