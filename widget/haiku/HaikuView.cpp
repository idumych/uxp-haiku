/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuView.h"
#include "nsWindow.h"

#include <cstring>

HaikuWindow::HaikuWindow(BRect aFrame, const char* aTitle, window_look aLook,
                         window_feel aFeel, uint32 aFlags,
                         nsWindow* aGeckoWindow)
    : BWindow(aFrame, aTitle, aLook, aFeel, aFlags),
      mGeckoWindow(aGeckoWindow) {
  // Let Gecko handle the usual editing/closing shortcuts.
  RemoveShortcut('W', B_COMMAND_KEY);
  RemoveShortcut('Q', B_COMMAND_KEY);
  RemoveShortcut('X', B_COMMAND_KEY);
  RemoveShortcut('C', B_COMMAND_KEY);
  RemoveShortcut('V', B_COMMAND_KEY);
  RemoveShortcut('A', B_COMMAND_KEY);
}

void HaikuWindow::DispatchMessage(BMessage* aMessage, BHandler* aHandler) {
  switch (aMessage->what) {
    case B_UNMAPPED_KEY_DOWN:
    case B_UNMAPPED_KEY_UP:
    case B_KEY_DOWN:
    case B_KEY_UP: {
      if (!mGeckoWindow) {
        break;
      }
      int32 key = 0, rawChar = 0, modifiers = 0, repeat = 0;
      const char* bytes = nullptr;
      aMessage->FindInt32("key", &key);
      if (aMessage->FindInt32("modifiers", &modifiers) != B_OK) {
        modifiers = ::modifiers();
      }
      aMessage->FindInt32("raw_char", &rawChar);
      aMessage->FindInt32("be:key_repeat", &repeat);
      aMessage->FindString("bytes", &bytes);
      bool isDown = aMessage->what == B_KEY_DOWN ||
                    aMessage->what == B_UNMAPPED_KEY_DOWN;
      mGeckoWindow->OnHaikuKeyEvent(isDown, key, rawChar, modifiers,
                                   bytes ? bytes : "",
                                   bytes ? strlen(bytes) : 0, repeat > 0);
      // Gecko handles keys itself; don't let BWindow act on them.
      return;
    }
    default:
      break;
  }
  BWindow::DispatchMessage(aMessage, aHandler);
}

bool HaikuWindow::QuitRequested() {
  if (mGeckoWindow) {
    mGeckoWindow->OnHaikuQuitRequested();
    return false;
  }
  return true;
}

void HaikuWindow::WindowActivated(bool aActive) {
  if (mGeckoWindow) {
    mGeckoWindow->OnHaikuWindowActivated(aActive);
  }
  BWindow::WindowActivated(aActive);
}

void HaikuWindow::FrameMoved(BPoint aNewPosition) {
  BWindow::FrameMoved(aNewPosition);
  if (mGeckoWindow) {
    mGeckoWindow->OnHaikuFrameMoved(int32_t(aNewPosition.x),
                                    int32_t(aNewPosition.y));
  }
}

void HaikuWindow::FrameResized(float aWidth, float aHeight) {
  BWindow::FrameResized(aWidth, aHeight);
  if (mGeckoWindow) {
    mGeckoWindow->OnHaikuFrameResized(int32_t(aWidth) + 1,
                                      int32_t(aHeight) + 1);
  }
}

void HaikuWindow::Zoom(BPoint aOrigin, float aWidth, float aHeight) {
  BRect before = Frame();
  BWindow::Zoom(aOrigin, aWidth, aHeight);
  BRect after = Frame();
  if (mGeckoWindow) {
    mGeckoWindow->OnHaikuZoom(after.Width() * after.Height() >
                              before.Width() * before.Height());
  }
}

void HaikuWindow::Minimize(bool aMinimize) {
  BWindow::Minimize(aMinimize);
  if (mGeckoWindow) {
    mGeckoWindow->OnHaikuMinimize(aMinimize);
  }
}

void HaikuWindow::MessageReceived(BMessage* aMessage) {
  if (aMessage->what == kMsgGeckoInvalidate) {
    BRect r;
    BView* view = ChildAt(0);
    if (aMessage->FindRect("rect", &r) == B_OK && r.IsValid() && view &&
        mGeckoWindow) {
      view->Invalidate(r);
    }
    return;
  }
  BWindow::MessageReceived(aMessage);
}

HaikuView::HaikuView(BRect aFrame, nsWindow* aGeckoWindow)
    : BView(aFrame, "GeckoRootView", B_FOLLOW_ALL,
            B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
      mGeckoWindow(aGeckoWindow) {
  SetViewColor(B_TRANSPARENT_COLOR);
}

void HaikuView::AttachedToWindow() {
  BView::AttachedToWindow();
  MakeFocus(true);
}

void HaikuView::Draw(BRect aUpdateRect) {
  if (mGeckoWindow) {
    mGeckoWindow->DrawToHaikuView(this, aUpdateRect);
  }
}

void HaikuView::GetCurrentMouseState(int32& aButtons, int32& aModifiers) {
  aButtons = 0;
  aModifiers = 0;
  BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
  if (msg) {
    msg->FindInt32("buttons", &aButtons);
    if (msg->FindInt32("modifiers", &aModifiers) != B_OK) {
      aModifiers = ::modifiers();
    }
  } else {
    aModifiers = ::modifiers();
  }
}

void HaikuView::MessageReceived(BMessage* aMessage) {
  switch (aMessage->what) {
    case B_MOUSE_WHEEL_CHANGED: {
      if (!mGeckoWindow) {
        break;
      }
      float deltaX = 0.0f, deltaY = 0.0f;
      int32 modifiers = 0;
      aMessage->FindFloat("be:wheel_delta_x", &deltaX);
      aMessage->FindFloat("be:wheel_delta_y", &deltaY);
      if (aMessage->FindInt32("modifiers", &modifiers) != B_OK) {
        modifiers = ::modifiers();
      }
      BPoint where;
      uint32 buttons = 0;
      GetMouse(&where, &buttons, false);
      mGeckoWindow->OnHaikuWheelEvent(where.x, where.y, deltaX, deltaY,
                                      modifiers);
      break;
    }
    default:
      BView::MessageReceived(aMessage);
      break;
  }
}

void HaikuView::MouseDown(BPoint aPoint) {
  if (!mGeckoWindow) {
    return;
  }
  int32 buttons = 0, clicks = 1, modifiers = 0;
  GetCurrentMouseState(buttons, modifiers);
  if (Window() && Window()->CurrentMessage()) {
    Window()->CurrentMessage()->FindInt32("clicks", &clicks);
  }
  int32 changed = buttons & ~mLastButtons;
  if (!changed) {
    changed = buttons;
  }
  mLastButtons = buttons;

  // Keep getting mouse events while a button is down, even outside.
  SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
  mGeckoWindow->OnHaikuMouseDown(aPoint.x, aPoint.y, changed, buttons, clicks,
                                 modifiers);
}

void HaikuView::MouseUp(BPoint aPoint) {
  if (!mGeckoWindow) {
    return;
  }
  int32 remaining = 0, modifiers = 0;
  GetCurrentMouseState(remaining, modifiers);
  int32 released = mLastButtons & ~remaining;
  if (!released) {
    released = mLastButtons;
  }
  mLastButtons = remaining;
  mGeckoWindow->OnHaikuMouseUp(aPoint.x, aPoint.y, released, remaining,
                               modifiers);
}

void HaikuView::MouseMoved(BPoint aPoint, uint32 aTransit,
                           const BMessage* aMessage) {
  if (!mGeckoWindow) {
    return;
  }
  int32 buttons = 0, modifiers = 0;
  GetCurrentMouseState(buttons, modifiers);
  mLastButtons = buttons;

  switch (aTransit) {
    case B_ENTERED_VIEW:
      mGeckoWindow->OnHaikuMouseCrossing(true, aPoint.x, aPoint.y, buttons,
                                         modifiers);
      mGeckoWindow->OnHaikuMouseMoved(aPoint.x, aPoint.y, buttons, modifiers);
      break;
    case B_EXITED_VIEW:
      mGeckoWindow->OnHaikuMouseCrossing(false, aPoint.x, aPoint.y, buttons,
                                         modifiers);
      break;
    case B_OUTSIDE_VIEW:
      if (buttons) {
        mGeckoWindow->OnHaikuMouseMoved(aPoint.x, aPoint.y, buttons,
                                        modifiers);
      }
      break;
    default:
      mGeckoWindow->OnHaikuMouseMoved(aPoint.x, aPoint.y, buttons, modifiers);
      break;
  }
}
