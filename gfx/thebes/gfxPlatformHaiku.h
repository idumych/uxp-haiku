/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_PLATFORM_HAIKU_H
#define GFX_PLATFORM_HAIKU_H

#include "gfxPlatform.h"
#include "nsTArray.h"

// Haiku: fontconfig/FreeType fonts, cairo image surfaces, basic compositor.
class gfxPlatformHaiku : public gfxPlatform {
public:
    gfxPlatformHaiku();
    virtual ~gfxPlatformHaiku();

    static gfxPlatformHaiku *GetPlatform() {
        return (gfxPlatformHaiku*) gfxPlatform::GetPlatform();
    }

    virtual already_AddRefed<gfxASurface>
      CreateOffscreenSurface(const IntSize& aSize,
                             gfxImageFormat aFormat) override;

    virtual nsresult GetFontList(nsIAtom *aLangGroup,
                                 const nsACString& aGenericFamily,
                                 nsTArray<nsString>& aListOfFonts) override;

    virtual nsresult UpdateFontList() override;

    virtual void
    GetCommonFallbackFonts(uint32_t aCh, uint32_t aNextCh,
                           Script aRunScript,
                           nsTArray<const char*>& aFontList) override;

    virtual gfxPlatformFontList* CreatePlatformFontList() override;

    virtual nsresult GetStandardFamilyName(const nsAString& aFontName,
                                           nsAString& aFamilyName) override;

    gfxFontGroup*
    CreateFontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                    const gfxFontStyle *aStyle,
                    gfxTextPerfMetrics* aTextPerf,
                    gfxUserFontSet *aUserFontSet,
                    gfxFloat aDevToCssSize) override;

    virtual gfxFontEntry* LookupLocalFont(const nsAString& aFontName,
                                          uint16_t aWeight,
                                          int16_t aStretch,
                                          uint8_t aStyle) override;

    virtual gfxFontEntry* MakePlatformFont(const nsAString& aFontName,
                                           uint16_t aWeight,
                                           int16_t aStretch,
                                           uint8_t aStyle,
                                           const uint8_t* aFontData,
                                           uint32_t aLength) override;

    virtual bool IsFontFormatSupported(nsIURI *aFontURI,
                                       uint32_t aFormatFlags) override;

    virtual gfxImageFormat GetOffscreenFormat() override;

    bool SupportsApzWheelInput() const override {
      return true;
    }

    void FontsPrefsChanged(const char *aPref) override;

    // maximum number of fonts to substitute for a generic
    uint32_t MaxGenericSubstitions();

protected:
    int8_t mMaxGenericSubstitutions;

private:
    virtual void GetPlatformCMSOutputProfile(void *&mem,
                                             size_t &size) override;
};

#endif /* GFX_PLATFORM_HAIKU_H */
