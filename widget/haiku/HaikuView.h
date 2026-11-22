/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuView_h
#define mozilla_widget_HaikuView_h

#include "HaikuUtils.h"

class nsWindow;

static const uint32 kMsgGeckoInvalidate = 'GINV';

// Each top-level nsWindow owns one HaikuWindow (its own looper thread) with
// a single HaikuView. Both only forward events to nsWindow, which re-posts
// them to the Gecko main thread.
class HaikuWindow : public BWindow {
 public:
  using BWindow::Zoom;

  HaikuWindow(BRect aFrame, const char* aTitle, window_look aLook,
              window_feel aFeel, uint32 aFlags, nsWindow* aGeckoWindow);

  void DispatchMessage(BMessage* aMessage, BHandler* aHandler) override;
  bool QuitRequested() override;
  void WindowActivated(bool aActive) override;
  void MessageReceived(BMessage* aMessage) override;
  void FrameMoved(BPoint aNewPosition) override;
  void FrameResized(float aWidth, float aHeight) override;
  void Zoom(BPoint aOrigin, float aWidth, float aHeight) override;
  void Minimize(bool aMinimize) override;
  void WorkspaceActivated(int32 aWorkspace, bool aActive) override;

  void DetachGeckoWindow() { mGeckoWindow = nullptr; }

 private:
  nsWindow* mGeckoWindow;
};

class HaikuView : public BView {
 public:
  HaikuView(BRect aFrame, nsWindow* aGeckoWindow);

  void AttachedToWindow() override;
  void Draw(BRect aUpdateRect) override;
  void MessageReceived(BMessage* aMessage) override;
  void MouseDown(BPoint aPoint) override;
  void MouseUp(BPoint aPoint) override;
  void MouseMoved(BPoint aPoint, uint32 aTransit,
                  const BMessage* aMessage) override;

  void DetachGeckoWindow() { mGeckoWindow = nullptr; }

 private:
  void GetCurrentMouseState(int32& aButtons, int32& aModifiers);
  void WriteCopyTarget(BMessage* aMessage);

  nsWindow* mGeckoWindow;
  int32 mLastButtons = 0;
};

#endif  // mozilla_widget_HaikuView_h
