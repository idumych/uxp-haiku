/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuFilePanel_h
#define mozilla_widget_HaikuFilePanel_h

#include "nsStringFwd.h"
#include "nsTArrayForwardDeclare.h"

// Haiku API classes need default visibility (see nsFilePicker.h).
#pragma GCC visibility push(default)
class BLooper;
class BRefFilter;
#pragma GCC visibility pop
class nsFilePicker;

namespace mozilla {
namespace widget {

template <class T> class MainThreadWeakRef;

// In HaikuFilePanel.cpp (built with RTTI).
BRefFilter* CreatePatternRefFilter(const nsAString& aFilter);
BLooper* CreateFilePanelLooper(nsFilePicker* aPicker);
void DetachFilePanelLooper(BLooper* aLooper);

// In nsFilePicker.cpp (no RTTI): hands the panel's result to the main thread.
void DeliverFilePanelResult(MainThreadWeakRef<nsFilePicker>* aRef,
                            int16_t aResult, nsTArray<nsCString>&& aPaths);

} // namespace widget
} // namespace mozilla

#endif
