/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsClipboard_h_
#define nsClipboard_h_

#include "nsCOMPtr.h"
#include "nsBaseClipboard.h"

class nsClipboard final : public nsBaseClipboard
{
public:
  nsClipboard();

  NS_IMETHOD HasDataMatchingFlavors(const char** aFlavorList,
                                    uint32_t aLength,
                                    int32_t aWhichClipboard,
                                    bool* aResult) override;

protected:
  virtual ~nsClipboard();

  NS_IMETHOD SetNativeClipboardData(int32_t aWhichClipboard) override;
  NS_IMETHOD GetNativeClipboardData(nsITransferable* aTransferable,
                                    int32_t aWhichClipboard) override;
};

#endif // nsClipboard_h_
