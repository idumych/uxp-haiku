/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Based on gfxPlatformGtk without the GTK/X11 parts.

#include "gfxPlatformHaiku.h"

#include "nsUnicodeProperties.h"
#include "gfxFcPlatformFontList.h"
#include "gfxImageSurface.h"
#include "gfxPrefs.h"
#include "gfxTextRun.h"
#include "gfxUserFontSet.h"
#include "gfxUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/gfx/2D.h"

#include <fontconfig/fontconfig.h>

#define GFX_PREF_MAX_GENERIC_SUBSTITUTIONS "gfx.font_rendering.fontconfig.max_generic_substitutions"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::unicode;

gfxPlatformHaiku::gfxPlatformHaiku()
{
    mMaxGenericSubstitutions = UNINITIALIZED_VALUE;

    uint32_t canvasMask = BackendTypeBit(BackendType::CAIRO);
    uint32_t contentMask = BackendTypeBit(BackendType::CAIRO);
#ifdef USE_SKIA
    canvasMask |= BackendTypeBit(BackendType::SKIA);
    contentMask |= BackendTypeBit(BackendType::SKIA);
#endif
    InitBackendPrefs(canvasMask, BackendType::CAIRO,
                     contentMask, BackendType::CAIRO);
}

gfxPlatformHaiku::~gfxPlatformHaiku()
{
}

already_AddRefed<gfxASurface>
gfxPlatformHaiku::CreateOffscreenSurface(const IntSize& aSize,
                                         gfxImageFormat aFormat)
{
    if (!Factory::AllowedSurfaceSize(aSize)) {
        return nullptr;
    }

    // The gfxImageSurface ctor zeroes the data for us.
    RefPtr<gfxASurface> newSurface = new gfxImageSurface(aSize, aFormat);
    if (newSurface->CairoStatus()) {
        return nullptr;
    }
    return newSurface.forget();
}

nsresult
gfxPlatformHaiku::GetFontList(nsIAtom *aLangGroup,
                              const nsACString& aGenericFamily,
                              nsTArray<nsString>& aListOfFonts)
{
    gfxPlatformFontList::PlatformFontList()->GetFontList(aLangGroup,
                                                         aGenericFamily,
                                                         aListOfFonts);
    return NS_OK;
}

nsresult
gfxPlatformHaiku::UpdateFontList()
{
    gfxPlatformFontList::PlatformFontList()->UpdateFontList();
    return NS_OK;
}

// Fonts shipped with Haiku (Noto, DejaVu) plus common extras.
static const char kFontNotoSans[] = "Noto Sans";
static const char kFontNotoSerif[] = "Noto Serif";
static const char kFontDejaVuSans[] = "DejaVu Sans";
static const char kFontDejaVuSerif[] = "DejaVu Serif";
static const char kFontNotoColorEmoji[] = "Noto Color Emoji";
static const char kFontTwemojiMozilla[] = "Twemoji Mozilla";
static const char kFontNotoSansCJK[] = "Noto Sans CJK JP";
static const char kFontDroidSansFallback[] = "Droid Sans Fallback";

void
gfxPlatformHaiku::GetCommonFallbackFonts(uint32_t aCh, uint32_t aNextCh,
                                         Script aRunScript,
                                         nsTArray<const char*>& aFontList)
{
    EmojiPresentation emoji = GetEmojiPresentation(aCh);
    EmojiPresentation eNext = GetEmojiPresentation(aNextCh, true);
    if (aNextCh != kVariationSelector15 &&
        emoji != EmojiPresentation::TextOnly &&
        (emoji != EmojiPresentation::TextDefault ||
         eNext == EmojiPresentation::EmojiComponent)) {
        aFontList.AppendElement(kFontTwemojiMozilla);
        aFontList.AppendElement(kFontNotoColorEmoji);
    }

    aFontList.AppendElement(kFontNotoSerif);
    aFontList.AppendElement(kFontDejaVuSerif);
    aFontList.AppendElement(kFontNotoSans);
    aFontList.AppendElement(kFontDejaVuSans);

    // add fonts for CJK ranges
    if (aCh >= 0x3000 &&
        ((aCh < 0xe000) ||
         (aCh >= 0xf900 && aCh < 0xfff0) ||
         ((aCh >> 16) == 2))) {
        aFontList.AppendElement(kFontNotoSansCJK);
        aFontList.AppendElement(kFontDroidSansFallback);
    }
}

gfxPlatformFontList*
gfxPlatformHaiku::CreatePlatformFontList()
{
    gfxPlatformFontList* list = new gfxFcPlatformFontList();
    if (NS_SUCCEEDED(list->InitFontList())) {
        return list;
    }
    gfxPlatformFontList::Shutdown();
    return nullptr;
}

nsresult
gfxPlatformHaiku::GetStandardFamilyName(const nsAString& aFontName,
                                        nsAString& aFamilyName)
{
    gfxPlatformFontList::PlatformFontList()->
        GetStandardFamilyName(aFontName, aFamilyName);
    return NS_OK;
}

gfxFontGroup *
gfxPlatformHaiku::CreateFontGroup(const FontFamilyList& aFontFamilyList,
                                  const gfxFontStyle* aStyle,
                                  gfxTextPerfMetrics* aTextPerf,
                                  gfxUserFontSet* aUserFontSet,
                                  gfxFloat aDevToCssSize)
{
    return new gfxFontGroup(aFontFamilyList, aStyle, aTextPerf,
                            aUserFontSet, aDevToCssSize);
}

gfxFontEntry*
gfxPlatformHaiku::LookupLocalFont(const nsAString& aFontName,
                                  uint16_t aWeight,
                                  int16_t aStretch,
                                  uint8_t aStyle)
{
    gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
    return pfl->LookupLocalFont(aFontName, aWeight, aStretch, aStyle);
}

gfxFontEntry*
gfxPlatformHaiku::MakePlatformFont(const nsAString& aFontName,
                                   uint16_t aWeight,
                                   int16_t aStretch,
                                   uint8_t aStyle,
                                   const uint8_t* aFontData,
                                   uint32_t aLength)
{
    gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
    return pfl->MakePlatformFont(aFontName, aWeight, aStretch,
                                 aStyle, aFontData, aLength);
}

bool
gfxPlatformHaiku::IsFontFormatSupported(nsIURI *aFontURI, uint32_t aFormatFlags)
{
    // check for strange format flags
    NS_ASSERTION(!(aFormatFlags & gfxUserFontSet::FLAG_FORMAT_NOT_USED),
                 "strange font format hint set");

    // accept supported formats
    if (aFormatFlags & gfxUserFontSet::FLAG_FORMATS_COMMON) {
        return true;
    }

    // reject all other formats, known and unknown
    if (aFormatFlags != 0) {
        return false;
    }

    // no format hint set, need to look at data
    return true;
}

gfxImageFormat
gfxPlatformHaiku::GetOffscreenFormat()
{
    // app_server screens are 32 bpp.
    return SurfaceFormat::X8R8G8B8_UINT32;
}

void
gfxPlatformHaiku::FontsPrefsChanged(const char *aPref)
{
    // only checking for generic substitions, pass other changes up
    if (strcmp(GFX_PREF_MAX_GENERIC_SUBSTITUTIONS, aPref)) {
        gfxPlatform::FontsPrefsChanged(aPref);
        return;
    }

    mMaxGenericSubstitutions = UNINITIALIZED_VALUE;
    gfxFcPlatformFontList* pfl = gfxFcPlatformFontList::PlatformFontList();
    pfl->ClearGenericMappings();
    FlushFontAndWordCaches();
}

uint32_t
gfxPlatformHaiku::MaxGenericSubstitions()
{
    if (mMaxGenericSubstitutions == UNINITIALIZED_VALUE) {
        mMaxGenericSubstitutions =
            Preferences::GetInt(GFX_PREF_MAX_GENERIC_SUBSTITUTIONS, 3);
        if (mMaxGenericSubstitutions < 0) {
            mMaxGenericSubstitutions = 3;
        }
    }

    return uint32_t(mMaxGenericSubstitutions);
}

void
gfxPlatformHaiku::GetPlatformCMSOutputProfile(void *&mem, size_t &size)
{
    // No display color profile on Haiku.
    mem = nullptr;
    size = 0;
}
