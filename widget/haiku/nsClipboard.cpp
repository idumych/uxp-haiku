/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Haiku clipboard: text/unicode and text/html via be_clipboard. The system
 * clipboard stores text as UTF-8 under the "text/plain" and "text/html"
 * B_MIME_TYPE fields. Images are not supported yet. */

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
      }
    }
  }

  be_clipboard->Unlock();
  return NS_OK;
}
