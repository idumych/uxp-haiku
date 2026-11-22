/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Haiku clipboard via be_clipboard. Text is stored as UTF-8 under the
 * "text/plain" and "text/html" B_MIME_TYPE fields. Images are stored both as
 * PNG data ("image/png", B_MIME_TYPE) and as an archived BBitmap
 * ("image/bitmap", like ShowImage does); either is read back as PNG. */

#include "nsClipboard.h"

#include "HaikuUtils.h"
#include "nsArrayUtils.h"
#include "nsCOMPtr.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "nsPrimitiveHelpers.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsXPIDLString.h"

#include <string.h>

#include "imgIContainer.h"
#include "nsIInputStream.h"
#include "nsStringStream.h"

#pragma GCC visibility push(default)
#include <translation/TranslatorFormats.h>
#pragma GCC visibility pop

using namespace mozilla::widget;

static bool IsImageFlavor(const char* aFlavor)
{
  return !strcmp(aFlavor, kPNGImageMime) || !strcmp(aFlavor, kJPEGImageMime) ||
         !strcmp(aFlavor, kJPGImageMime) || !strcmp(aFlavor, kGIFImageMime) ||
         !strcmp(aFlavor, kNativeImageMime);
}

// Translates a BBitmap to PNG data with the Translation Kit.
static bool BitmapToPNG(BBitmap* aBitmap, BMallocIO& aOut)
{
  BBitmapStream stream(aBitmap);  // takes ownership
  bool ok = BTranslatorRoster::Default()->Translate(&stream, nullptr, nullptr,
                                                    &aOut, B_PNG_FORMAT) == B_OK;
  BBitmap* detached = nullptr;
  stream.DetachBitmap(&detached);
  delete detached;
  return ok && aOut.BufferLength() > 0;
}

static void AddImageToClipboard(BMessage* aClip, imgIContainer* aImage)
{
  BBitmap* bitmap = CreateBBitmapFromImage(aImage);
  if (!bitmap) {
    return;
  }
  BMessage archive;
  if (bitmap->Archive(&archive) == B_OK) {
    aClip->AddMessage("image/bitmap", &archive);
  }
  BMallocIO png;
  if (BitmapToPNG(bitmap, png)) {  // consumes bitmap
    aClip->AddData("image/png", B_MIME_TYPE, png.Buffer(), png.BufferLength());
  }
}

// Returns PNG bytes from the clipboard as an input stream.
static already_AddRefed<nsIInputStream> GetClipboardImage(BMessage* aClip)
{
  const void* data = nullptr;
  ssize_t len = 0;
  nsCOMPtr<nsIInputStream> stream;
  if (aClip->FindData("image/png", B_MIME_TYPE, &data, &len) == B_OK &&
      len > 0) {
    NS_NewByteInputStream(getter_AddRefs(stream),
                          static_cast<const char*>(data), len,
                          NS_ASSIGNMENT_COPY);
    return stream.forget();
  }

  BMessage archive;
  if (aClip->FindMessage("image/bitmap", &archive) != B_OK) {
    return nullptr;
  }
  // BBitmap::Instantiate() checks the archive is a BBitmap.
  BBitmap* bitmap = static_cast<BBitmap*>(BBitmap::Instantiate(&archive));
  BMallocIO png;
  if (!bitmap || !BitmapToPNG(bitmap, png)) {
    return nullptr;
  }
  NS_NewByteInputStream(getter_AddRefs(stream),
                        static_cast<const char*>(png.Buffer()),
                        png.BufferLength(), NS_ASSIGNMENT_COPY);
  return stream.forget();
}

// Gecko flavor -> Haiku clipboard MIME field
static const char*
HaikuMimeForFlavor(const char* aFlavor)
{
  if (!strcmp(aFlavor, kUnicodeMime) || !strcmp(aFlavor, kTextMime)) {
    return "text/plain";
  }
  if (!strcmp(aFlavor, kHTMLMime)) {
    return "text/html";
  }
  return nullptr;
}

nsClipboard::nsClipboard()
  : nsBaseClipboard()
{
}

nsClipboard::~nsClipboard()
{
}

NS_IMETHODIMP
nsClipboard::SetNativeClipboardData(int32_t aWhichClipboard)
{
  if (aWhichClipboard != kGlobalClipboard || !mTransferable) {
    return NS_ERROR_FAILURE;
  }
  if (!be_clipboard || !be_clipboard->Lock()) {
    return NS_ERROR_FAILURE;
  }

  be_clipboard->Clear();
  BMessage* clip = be_clipboard->Data();
  if (!clip) {
    be_clipboard->Unlock();
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIArray> flavors;
  mTransferable->FlavorsTransferableCanExport(getter_AddRefs(flavors));
  uint32_t count = 0;
  if (flavors) {
    flavors->GetLength(&count);
  }

  bool wroteText = false;
  for (uint32_t i = 0; i < count; ++i) {
    nsCOMPtr<nsISupportsCString> flavorWrapper = do_QueryElementAt(flavors, i);
    if (!flavorWrapper) {
      continue;
    }
    nsXPIDLCString flavor;
    flavorWrapper->ToString(getter_Copies(flavor));

    if (!strcmp(flavor.get(), kNativeImageMime)) {
      nsCOMPtr<nsISupports> data;
      uint32_t len = 0;
      mTransferable->GetTransferData(flavor.get(), getter_AddRefs(data), &len);
      nsCOMPtr<nsISupportsInterfacePointer> ptr = do_QueryInterface(data);
      nsCOMPtr<nsISupports> primitive;
      if (ptr) {
        ptr->GetData(getter_AddRefs(primitive));
      }
      nsCOMPtr<imgIContainer> image = do_QueryInterface(primitive);
      if (image) {
        AddImageToClipboard(clip, image);
      }
      continue;
    }

    const char* haikuMime = HaikuMimeForFlavor(flavor.get());
    if (!haikuMime || (wroteText && !strcmp(haikuMime, "text/plain"))) {
      continue;
    }

    nsCOMPtr<nsISupports> data;
    uint32_t len = 0;
    if (NS_FAILED(mTransferable->GetTransferData(flavor.get(),
                                                 getter_AddRefs(data), &len)) ||
        !data) {
      continue;
    }

    nsAutoCString utf8;
    nsCOMPtr<nsISupportsString> wide = do_QueryInterface(data);
    nsCOMPtr<nsISupportsCString> narrow = do_QueryInterface(data);
    if (wide) {
      nsAutoString str;
      wide->GetData(str);
      CopyUTF16toUTF8(str, utf8);
    } else if (narrow) {
      narrow->GetData(utf8);
    } else {
      continue;
    }

    if (clip->AddData(haikuMime, B_MIME_TYPE, utf8.get(), utf8.Length()) ==
          B_OK &&
        !strcmp(haikuMime, "text/plain")) {
      wroteText = true;
    }
  }

  be_clipboard->Commit();
  be_clipboard->Unlock();
  return NS_OK;
}

NS_IMETHODIMP
nsClipboard::GetNativeClipboardData(nsITransferable* aTransferable,
                                    int32_t aWhichClipboard)
{
  if (!aTransferable || aWhichClipboard != kGlobalClipboard) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIArray> flavors;
  nsresult rv =
    aTransferable->FlavorsTransferableCanImport(getter_AddRefs(flavors));
  if (NS_FAILED(rv) || !flavors) {
    return NS_ERROR_FAILURE;
  }

  if (!be_clipboard || !be_clipboard->Lock()) {
    return NS_ERROR_FAILURE;
  }
  BMessage* clip = be_clipboard->Data();
  if (!clip) {
    be_clipboard->Unlock();
    return NS_ERROR_FAILURE;
  }

  uint32_t count = 0;
  flavors->GetLength(&count);
  for (uint32_t i = 0; i < count; ++i) {
    nsCOMPtr<nsISupportsCString> flavorWrapper = do_QueryElementAt(flavors, i);
    if (!flavorWrapper) {
      continue;
    }
    nsXPIDLCString flavor;
    flavorWrapper->ToString(getter_Copies(flavor));

    if (IsImageFlavor(flavor.get()) &&
        strcmp(flavor.get(), kNativeImageMime)) {
      nsCOMPtr<nsIInputStream> stream = GetClipboardImage(clip);
      if (stream) {
        // The data is PNG whatever image flavor was asked for; imagelib
        // sniffs the format.
        aTransferable->SetTransferData(flavor.get(), stream,
                                       sizeof(nsIInputStream*));
        break;
      }
      continue;
    }

    const char* haikuMime = HaikuMimeForFlavor(flavor.get());
    if (!haikuMime) {
      continue;
    }

    const void* data = nullptr;
    ssize_t dataLen = 0;
    if (clip->FindData(haikuMime, B_MIME_TYPE, &data, &dataLen) != B_OK ||
        !data || dataLen <= 0) {
      continue;
    }

    const char* text = static_cast<const char*>(data);
    if (text[dataLen - 1] == '\0') {
      dataLen--;
    }

    // text/unicode and text/html are both handed to Gecko as UTF-16.
    NS_ConvertUTF8toUTF16 wide(text, dataLen);
    const char* outFlavor =
      !strcmp(flavor.get(), kTextMime) ? kUnicodeMime : flavor.get();
    uint32_t byteLen = wide.Length() * sizeof(char16_t);

    nsCOMPtr<nsISupports> wrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(outFlavor, wide.get(), byteLen,
                                               getter_AddRefs(wrapper));
    if (wrapper) {
      aTransferable->SetTransferData(outFlavor, wrapper, byteLen);
    }
    break;
  }

  be_clipboard->Unlock();
  return NS_OK;
}

NS_IMETHODIMP
nsClipboard::HasDataMatchingFlavors(const char** aFlavorList,
                                    uint32_t aLength,
                                    int32_t aWhichClipboard,
                                    bool* aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = false;
  if (aWhichClipboard != kGlobalClipboard || !be_clipboard ||
      !be_clipboard->Lock()) {
    return NS_OK;
  }

  BMessage* clip = be_clipboard->Data();
  if (clip) {
    for (uint32_t i = 0; i < aLength && !*aResult; ++i) {
      const char* haikuMime = HaikuMimeForFlavor(aFlavorList[i]);
      if (haikuMime && clip->HasData(haikuMime, B_MIME_TYPE)) {
        *aResult = true;
      } else if (IsImageFlavor(aFlavorList[i]) &&
                 (clip->HasData("image/png", B_MIME_TYPE) ||
                  clip->HasMessage("image/bitmap"))) {
        *aResult = true;
      }
    }
  }

  be_clipboard->Unlock();
  return NS_OK;
}
