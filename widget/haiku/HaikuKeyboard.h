/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuKeyboard_h
#define mozilla_widget_HaikuKeyboard_h

#include <cstdint>
#include "mozilla/EventForwards.h"
#include "mozilla/TextEventDispatcherListener.h"

class nsWindow;

namespace mozilla::widget {

mozilla::Modifiers GetGeckoModifiers(int32_t aHaikuModifiers);

// Minimal native text listener: no IME yet, only plain key events.
class HaikuTextInputListener final : public TextEventDispatcherListener {
 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                       const IMENotification& aNotification) override;
  NS_IMETHOD_(void) OnRemovedFrom(
      TextEventDispatcher* aTextEventDispatcher) override {}
  NS_IMETHOD_(void) WillDispatchKeyboardEvent(
      TextEventDispatcher* aTextEventDispatcher,
      WidgetKeyboardEvent& aKeyboardEvent, uint32_t aIndexOfKeypress,
      void* aData) override {}

 private:
  ~HaikuTextInputListener() = default;
};

class HaikuKeyboard {
 public:
  // Called on the BWindow thread; dispatches to the main thread.
  static void DispatchKeyEvent(nsWindow* aWindow, bool aIsDown, int32_t aKey,
                               int32_t aRawChar, int32_t aModifiers,
                               const char* aBytes, int32_t aNumBytes,
                               bool aIsRepeat);
};

}  // namespace mozilla::widget

#endif  // mozilla_widget_HaikuKeyboard_h
