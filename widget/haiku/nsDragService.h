/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDragService_h__
#define nsDragService_h__

#include "nsBaseDragService.h"
#include "nsCOMPtr.h"
#include "nsIArray.h"
#include "mozilla/UniquePtr.h"

// Haiku API classes: declare them with default visibility, or gcc_hidden.h makes
// them hidden and libxul can't link against libbe.
#pragma GCC visibility push(default)
class BMessage;
#pragma GCC visibility pop
class nsWindow;

/*
 * Drag and drop with Be API drag messages (B_SIMPLE_DATA BMessages).
 *
 * Incoming: HaikuView forwards the drag BMessage to nsWindow, which keeps
 * the message here (SetDragMessage) and dispatches eDragEnter/Over/Exit and
 * eDrop. Supported data: text ("text/plain" B_MIME_TYPE, UTF-8), URLs
 * ("text/uri-list" or "be:url"), and files ("refs" entry_refs).
 *
 * Outgoing: InvokeDragSessionImpl builds such a BMessage from the first
 * transferable and starts BView::DragMessage(). The session ends on the next
 * mouse-up in the source window (or on a drop into one of our windows).
 */
class nsDragService final : public nsBaseDragService
{
public:
  nsDragService();

  static nsDragService* GetInstance();

  // nsIDragSession
  NS_IMETHOD GetNumDropItems(uint32_t* aNumItems) override;
  NS_IMETHOD GetData(nsITransferable* aTransferable,
                     uint32_t aItemIndex) override;
  NS_IMETHOD IsDataFlavorSupported(const char* aDataFlavor,
                                   bool* _retval) override;
  NS_IMETHOD EndDragSession(bool aDoneDrag) override;

  // Main thread. Takes a copy of the BMessage from the BWindow thread.
  void SetDragMessage(mozilla::UniquePtr<BMessage> aMessage);
  bool IsOutgoingDrag() const { return !!mSourceDataItems; }

protected:
  virtual ~nsDragService();
  nsresult InvokeDragSessionImpl(nsIArray* aTransferableArray,
                                 nsIScriptableRegion* aDragRgn,
                                 uint32_t aActionType) override;

private:
  nsCOMPtr<nsIArray> mSourceDataItems;
  mozilla::UniquePtr<BMessage> mDragMessage;
};

#endif // nsDragService_h__
