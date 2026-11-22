/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuKeyboard_h
#define mozilla_widget_HaikuKeyboard_h

#include <cstdint>
#include "mozilla/EventForwards.h"
#include "mozilla/TextEventDispatcherListener.h"
#include "mozilla/Mutex.h"
#include "HaikuUtils.h"

class nsWindow;

namespace mozilla::widget {

mozilla::Modifiers GetGeckoModifiers(int32_t aHaikuModifiers);

// Native text listener: plain key events plus a basic Be input method
// (B_INPUT_METHOD_EVENT) bridge.
class HaikuTextInputListener final : public TextEventDispatcherListener {
 public:
  // Thread-safe: the BWindow thread holds references while posting input
  // method events to the main thread.
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit HaikuTextInputListener(nsWindow* aWindow);
  void Detach();

  NS_IMETHOD NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                       const IMENotification& aNotification) override;
  NS_IMETHOD_(void) OnRemovedFrom(
      TextEventDispatcher* aTextEventDispatcher) override {}
  NS_IMETHOD_(void) WillDispatchKeyboardEvent(
      TextEventDispatcher* aTextEventDispatcher,
      WidgetKeyboardEvent& aKeyboardEvent, uint32_t aIndexOfKeypress,
      void* aData) override {}

  // Called on the BWindow thread with a B_INPUT_METHOD_EVENT.
  void ProcessInputMethodEvent(BMessage* aMessage);

 private:
  ~HaikuTextInputListener() = default;
  void UpdateCaretPosition();  // main thread

  nsWindow* mWindow;  // weak, cleared by Detach()
  mozilla::Mutex mLock;  // guards the fields below (shared with BWindow)
  BMessenger mReplyTo;
  BPoint mCaretScreenPos;
  float mCaretHeight;
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
