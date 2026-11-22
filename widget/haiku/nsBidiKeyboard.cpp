/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsBidiKeyboard.h"

NS_IMPL_ISUPPORTS(nsBidiKeyboard, nsIBidiKeyboard)

nsBidiKeyboard::nsBidiKeyboard()
{
}

nsBidiKeyboard::~nsBidiKeyboard()
{
}

NS_IMETHODIMP
nsBidiKeyboard::Reset()
{
  return NS_OK;
}

NS_IMETHODIMP
nsBidiKeyboard::IsLangRTL(bool* aIsRTL)
{
  // Haiku has no API to query the keymap's direction.
  *aIsRTL = false;
  return NS_OK;
}

NS_IMETHODIMP
nsBidiKeyboard::GetHaveBidiKeyboards(bool* aResult)
{
  *aResult = false;
  return NS_OK;
}
