/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuCursor.h"

#include "imgIContainer.h"
#include "mozilla/Move.h"

#include <algorithm>

namespace mozilla {
namespace widget {

/* static */ BCursorID
HaikuCursor::CursorIDFor(nsCursor aCursor)
{
  switch (aCursor) {
    case eCursor_wait:
    case eCursor_spinning:
      return B_CURSOR_ID_PROGRESS;
    case eCursor_select:
      return B_CURSOR_ID_I_BEAM;
    case eCursor_vertical_text:
      return B_CURSOR_ID_I_BEAM_HORIZONTAL;
    case eCursor_hyperlink:
      return B_CURSOR_ID_FOLLOW_LINK;
    case eCursor_crosshair:
    case eCursor_cell:
      return B_CURSOR_ID_CROSS_HAIR;
    case eCursor_move:
    case eCursor_all_scroll:
      return B_CURSOR_ID_MOVE;
    case eCursor_help:
      return B_CURSOR_ID_HELP;
    case eCursor_copy:
      return B_CURSOR_ID_COPY;
    case eCursor_alias:
      return B_CURSOR_ID_CREATE_LINK;
    case eCursor_context_menu:
      return B_CURSOR_ID_CONTEXT_MENU;
    case eCursor_none:
      return B_CURSOR_ID_NO_CURSOR;
    case eCursor_not_allowed:
    case eCursor_no_drop:
      return B_CURSOR_ID_NOT_ALLOWED;
    case eCursor_grab:
      return B_CURSOR_ID_GRAB;
    case eCursor_grabbing:
      return B_CURSOR_ID_GRABBING;
    case eCursor_col_resize:
    case eCursor_ew_resize:
      return B_CURSOR_ID_RESIZE_EAST_WEST;
    case eCursor_row_resize:
    case eCursor_ns_resize:
      return B_CURSOR_ID_RESIZE_NORTH_SOUTH;
    case eCursor_n_resize:
      return B_CURSOR_ID_RESIZE_NORTH;
    case eCursor_s_resize:
      return B_CURSOR_ID_RESIZE_SOUTH;
    case eCursor_w_resize:
      return B_CURSOR_ID_RESIZE_WEST;
    case eCursor_e_resize:
      return B_CURSOR_ID_RESIZE_EAST;
    case eCursor_nw_resize:
      return B_CURSOR_ID_RESIZE_NORTH_WEST;
    case eCursor_ne_resize:
      return B_CURSOR_ID_RESIZE_NORTH_EAST;
    case eCursor_sw_resize:
      return B_CURSOR_ID_RESIZE_SOUTH_WEST;
    case eCursor_se_resize:
      return B_CURSOR_ID_RESIZE_SOUTH_EAST;
    case eCursor_nwse_resize:
      return B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST;
    case eCursor_nesw_resize:
      return B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST;
    case eCursor_zoom_in:
      return B_CURSOR_ID_ZOOM_IN;
    case eCursor_zoom_out:
      return B_CURSOR_ID_ZOOM_OUT;
    case eCursor_standard:
    default:
      return B_CURSOR_ID_SYSTEM_DEFAULT;
  }
}

/* static */ const BCursor*
HaikuCursor::GetSystemCursor(BCursorID aId)
{
  // BCursorIDs are small consecutive integers; cache one BCursor per ID for
  // the lifetime of the process.
  static const int32 kMaxCursorID = 64;
  static BCursor* sCursors[kMaxCursorID] = {};
  int32 index = int32(aId);
  if (index < 0 || index >= kMaxCursorID) {
    index = int32(B_CURSOR_ID_SYSTEM_DEFAULT);
    aId = B_CURSOR_ID_SYSTEM_DEFAULT;
  }
  if (!sCursors[index]) {
    sCursors[index] = new BCursor(aId);
  }
  return sCursors[index];
}

/* static */ void
HaikuCursor::Apply(BView* aView, const BCursor* aCursor)
{
  if (!aView || !aCursor) {
    return;
  }
  BWindow* window = aView->Window();
  if (window && window->LockWithTimeout(10000) == B_OK) {
    aView->SetViewCursor(aCursor);
    window->Unlock();
  }
}

/* static */ void
HaikuCursor::SetCursor(BView* aView, nsCursor aCursor)
{
  Apply(aView, GetSystemCursor(CursorIDFor(aCursor)));
}

/* static */ bool
HaikuCursor::SetImageCursor(BView* aView, imgIContainer* aImage,
                            uint32_t aHotspotX, uint32_t aHotspotY,
                            UniquePtr<BCursor>& aHolder)
{
  if (!aImage) {
    return false;
  }
  BBitmap* bitmap = CreateBBitmapFromImage(aImage);
  if (!bitmap) {
    return false;
  }
  float maxX = bitmap->Bounds().Width();
  float maxY = bitmap->Bounds().Height();
  BPoint hotspot(std::min(float(aHotspotX), maxX),
                 std::min(float(aHotspotY), maxY));
  UniquePtr<BCursor> cursor = MakeUnique<BCursor>(bitmap, hotspot);
  delete bitmap;
  if (cursor->InitCheck() != B_OK) {
    return false;
  }
  Apply(aView, cursor.get());
  aHolder = Move(cursor);
  return true;
}

} // namespace widget
} // namespace mozilla
