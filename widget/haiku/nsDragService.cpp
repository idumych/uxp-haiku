/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDragService.h"

nsDragService::nsDragService()
{
}

nsDragService::~nsDragService()
{
}

nsresult
nsDragService::InvokeDragSessionImpl(nsIArray* aTransferableArray,
                                     nsIScriptableRegion* aDragRgn,
                                     uint32_t aActionType)
{
  // TODO: start a BView::DragMessage() drag (see the Firefox Haiku port).
  return NS_ERROR_NOT_IMPLEMENTED;
}
