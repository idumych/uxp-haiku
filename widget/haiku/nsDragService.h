/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDragService_h__
#define nsDragService_h__

#include "nsBaseDragService.h"

/* Placeholder drag service: native drag and drop is not implemented yet, so
 * drags never start (InvokeDragSessionImpl fails) and nothing is dropped. */
class nsDragService final : public nsBaseDragService
{
public:
  nsDragService();

protected:
  virtual ~nsDragService();

  nsresult InvokeDragSessionImpl(nsIArray* aTransferableArray,
                                 nsIScriptableRegion* aDragRgn,
                                 uint32_t aActionType) override;
};

#endif // nsDragService_h__
