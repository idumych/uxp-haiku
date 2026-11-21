/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __nsLookAndFeel
#define __nsLookAndFeel

#include "nsXPLookAndFeel.h"
#include "gfxFont.h"

class nsLookAndFeel final : public nsXPLookAndFeel
{
public:
  nsLookAndFeel();
  virtual ~nsLookAndFeel();

  nsresult NativeGetColor(ColorID aID, nscolor& aResult) override;
  nsresult GetIntImpl(IntID aID, int32_t& aResult) override;
  nsresult GetFloatImpl(FloatID aID, float& aResult) override;
  bool GetFontImpl(FontID aID, nsString& aFontName,
                   gfxFontStyle& aFontStyle,
                   float aDevPixPerCSSPixel) override;
  void RefreshImpl() override;

private:
  struct SystemFont {
    nsString mName;
    gfxFontStyle mStyle;
  };

  void EnsureInit();
  static void InitSystemFont(const char* aFamily, float aSize,
                             uint16_t aFace, SystemFont& aFont);

  bool mInitialized;
  SystemFont mPlainFont;
  SystemFont mMenuFont;
};

#endif
