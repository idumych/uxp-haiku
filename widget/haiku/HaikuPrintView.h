/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuPrintView_h
#define mozilla_widget_HaikuPrintView_h

#include <interface/Rect.h>

// Haiku API classes need default visibility (see nsFilePicker.h).
#pragma GCC visibility push(default)
class BBitmap;
class BView;
#pragma GCC visibility pop

namespace mozilla {
namespace widget {

// In HaikuPrintView.cpp (built with RTTI).
BView* CreatePrintPageView(BRect aFrame);
void SetPrintPage(BView* aView, BBitmap* aPage, BRect aDest);

} // namespace widget
} // namespace mozilla

#endif
