/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindow.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "HaikuCursor.h"
#include "HaikuKeyboard.h"
#include "HaikuUtils.h"
#include "HaikuView.h"

#include "ClientLayerManager.h"
#include "gfxContext.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/TextEvents.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsIDOMWheelEvent.h"
#include "nsIRollupListener.h"
#include "nsThreadUtils.h"
#include "nsDragService.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;

NS_IMPL_ISUPPORTS_INHERITED0(nsWindow, nsBaseWidget)


// Haiku's menu window feel (kMenuWindowFeel in the app_server).
static const window_feel kHaikuMenuWindowFeel = window_feel(1025);

static int16_t GetGeckoButtons(int32_t aHaikuButtons) {
  int16_t buttons = 0;
  if (aHaikuButtons & B_PRIMARY_MOUSE_BUTTON) {
    buttons |= WidgetMouseEvent::eLeftButtonFlag;
  }
  if (aHaikuButtons & B_SECONDARY_MOUSE_BUTTON) {
    buttons |= WidgetMouseEvent::eRightButtonFlag;
  }
  if (aHaikuButtons & B_TERTIARY_MOUSE_BUTTON) {
    buttons |= WidgetMouseEvent::eMiddleButtonFlag;
  }
  return buttons;
}

static int16_t GetGeckoButton(int32_t aHaikuButton) {
  if (aHaikuButton & B_SECONDARY_MOUSE_BUTTON) {
    return WidgetMouseEvent::eRightButton;
  }
  if (aHaikuButton & B_TERTIARY_MOUSE_BUTTON) {
    return WidgetMouseEvent::eMiddleButton;
  }
  return WidgetMouseEvent::eLeftButton;
}

nsWindow::nsWindow()
    : mWindow(nullptr),
      mView(nullptr),
      mVisible(false),
      mEnabled(true),
      mIsPopup(false),
      mSettingSizeMode(false),
      mPaintScheduled(false) {}

nsWindow::~nsWindow() { Destroy(); }

nsIWidgetListener* nsWindow::GetPaintListener() {
  return mAttachedWidgetListener ? mAttachedWidgetListener : mWidgetListener;
}

// mBounds holds the client area in screen coordinates (top-level windows
// have no client offset: the BView fills the BWindow).
void nsWindow::UpdateBounds() {
  if (!mWindow) {
    return;
  }
  BRect f = mWindow->Frame();
  mBounds = LayoutDeviceIntRect(int32_t(f.left), int32_t(f.top),
                                std::max(1, int32_t(f.Width()) + 1),
                                std::max(1, int32_t(f.Height()) + 1));
}

nsresult nsWindow::Create(nsIWidget* aParent, nsNativeWidget aNativeParent,
                          const LayoutDeviceIntRect& aRect,
                          nsWidgetInitData* aInitData) {
  EnsureHaikuApplication();
  BaseCreate(aParent, aInitData);

  mParent = aParent;
  mBounds = aRect;
  mTextListener = new HaikuTextInputListener(this);
  mWeakRef = new MainThreadWeakRef<nsWindow>(this);

  if (mWindowType == eWindowType_child || mWindowType == eWindowType_plugin ||
      mWindowType == eWindowType_plugin_ipc_chrome ||
      mWindowType == eWindowType_plugin_ipc_content) {
    // No native window for child widgets.
    return NS_OK;
  }

  mIsPopup = mWindowType == eWindowType_popup;

  window_look look = B_TITLED_WINDOW_LOOK;
  window_feel feel = B_NORMAL_WINDOW_FEEL;
  uint32 flags = B_ASYNCHRONOUS_CONTROLS;

  switch (mWindowType) {
    case eWindowType_popup:
      look = B_NO_BORDER_WINDOW_LOOK;
      feel = kHaikuMenuWindowFeel;
      flags = B_AVOID_FOCUS | B_NO_WORKSPACE_ACTIVATION |
              B_NOT_ANCHORED_ON_ACTIVATE | B_NOT_MOVABLE | B_NOT_RESIZABLE |
              B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE;
      break;
    case eWindowType_invisible:
      look = B_NO_BORDER_WINDOW_LOOK;
      flags = B_AVOID_FOCUS | B_NO_WORKSPACE_ACTIVATION | B_NOT_MOVABLE |
              B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE;
      break;
    case eWindowType_dialog:
      if (aParent) {
        feel = B_FLOATING_APP_WINDOW_FEEL;
        flags |= B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE;
      }
      break;
    default:
      if (mBorderStyle == eBorderStyle_none) {
        look = B_NO_BORDER_WINDOW_LOOK;
      }
      break;
  }

  int32_t width = std::max(1, aRect.width);
  int32_t height = std::max(1, aRect.height);
  BRect frame(aRect.x, aRect.y, aRect.x + width - 1, aRect.y + height - 1);

  mWindow = new HaikuWindow(frame, "", look, feel, flags, this);
  mWindow->SetSizeLimits(1, 32767, 1, 32767);
  mView = new HaikuView(mWindow->Bounds(), this);
  mWindow->AddChild(mView);
  UpdateBounds();
  mBuffer.SetTarget(BMessenger(mWindow));

  // The BWindow's looper runs once it is shown for the first time; start it
  // hidden so that Show() can just Show()/Hide().
  mWindow->Hide();
  mWindow->Show();

  return NS_OK;
}

void nsWindow::Destroy() {
  if (mOnDestroyCalled) {
    return;
  }
  mOnDestroyCalled = true;

  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);

  // Tear down the compositor before the BWindow it draws into.
  if (mLayerManager) {
    mLayerManager->Destroy();
  }
  mLayerManager = nullptr;
  DestroyCompositor();

  if (gRollupListener) {
    nsCOMPtr<nsIWidget> rollupWidget = gRollupListener->GetRollupWidget();
    if (rollupWidget == static_cast<nsIWidget*>(this)) {
      gRollupListener->Rollup(0, false, nullptr, nullptr);
    }
  }

  if (mWeakRef) {
    mWeakRef->Clear();
  }

  if (mWindow) {
    // Detach under the window lock: the BWindow thread only uses its
    // nsWindow pointer from handlers, which run with the looper locked.
    mBuffer.SetTarget(BMessenger());
    if (mWindow->Lock()) {
      mView->DetachGeckoWindow();
      mWindow->DetachGeckoWindow();
      mWindow->Quit();  // deletes the BWindow and its views
    }
    mWindow = nullptr;
    mView = nullptr;
  }
  mBuffer.Destroy();
  if (mTextListener) {
    mTextListener->Detach();
  }

  nsBaseWidget::OnDestroy();
  nsBaseWidget::Destroy();
  mParent = nullptr;
  NotifyWindowDestroyed();
}

NS_IMETHODIMP nsWindow::Show(bool aState) {
  if (aState == mVisible) {
    return NS_OK;
  }
  mVisible = aState;
  if (!mWindow) {
    return NS_OK;
  }

  if (mWindow->Lock()) {
    if (aState) {
      // Windows (popups especially) may be reused after the user switched
      // workspaces: show them on the current one.
      if (mWindow->IsHidden()) {
        mWindow->SetWorkspaces(B_CURRENT_WORKSPACE);
        mWindow->Show();
      }
      if (mWindow->IsMinimized()) {
        mWindow->Minimize(false);
      }
    } else if (!mWindow->IsHidden()) {
      mWindow->Hide();
    }
    mWindow->Unlock();
  }

  if (aState) {
    SchedulePaint();
  }
  return NS_OK;
}

NS_IMETHODIMP nsWindow::Enable(bool aState) {
  mEnabled = aState;
  return NS_OK;
}

NS_IMETHODIMP nsWindow::SetFocus(bool aRaise) {
  if (!mWindow || mIsPopup) {
    return NS_OK;
  }
  if (mWindow->Lock()) {
    if (aRaise) {
      mWindow->Activate(true);
    }
    mView->MakeFocus(true);
    mWindow->Unlock();
  }
  return NS_OK;
}

NS_IMETHODIMP nsWindow::Move(double aX, double aY) {
  int32_t x = NSToIntRound(aX);
  int32_t y = NSToIntRound(aY);
  if (!mWindow) {
    mBounds.MoveTo(x, y);
    return NS_OK;
  }
  if (mWindow->Lock()) {
    mWindow->MoveTo(x, y);
    UpdateBounds();
    mWindow->Unlock();
  }
  NotifyWindowMoved(mBounds.x, mBounds.y);
  return NS_OK;
}

NS_IMETHODIMP nsWindow::Resize(double aWidth, double aHeight, bool aRepaint) {
  return Resize(mBounds.x, mBounds.y, aWidth, aHeight, aRepaint);
}

NS_IMETHODIMP nsWindow::Resize(double aX, double aY, double aWidth,
                               double aHeight, bool aRepaint) {
  int32_t x = NSToIntRound(aX);
  int32_t y = NSToIntRound(aY);
  int32_t w = std::max(1, NSToIntRound(aWidth));
  int32_t h = std::max(1, NSToIntRound(aHeight));
  bool moved = x != mBounds.x || y != mBounds.y;

  if (!mWindow) {
    mBounds = LayoutDeviceIntRect(x, y, w, h);
  } else if (mWindow->Lock()) {
    mWindow->MoveTo(x, y);
    mWindow->ResizeTo(w - 1, h - 1);
    UpdateBounds();
    mWindow->Unlock();
  }

  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, mBounds.width, mBounds.height);
  }
  if (moved) {
    NotifyWindowMoved(mBounds.x, mBounds.y);
  }
  if (aRepaint) {
    SchedulePaint();
  }
  return NS_OK;
}

void nsWindow::SetSizeMode(nsSizeMode aMode) {
  if (aMode == mSizeMode || !mWindow) {
    return;
  }
  if (aMode == nsSizeMode_Fullscreen) {
    MakeFullScreen(true);
    return;
  }
  if (mSizeMode == nsSizeMode_Fullscreen) {
    MakeFullScreen(false);
    if (aMode == nsSizeMode_Normal) {
      return;
    }
  }

  if (mSizeMode == nsSizeMode_Normal) {
    mRestoredBounds = Some(mBounds);
  }
  nsSizeMode oldMode = mSizeMode;
  mSizeMode = aMode;
  mSettingSizeMode = true;

  if (mWindow->Lock()) {
    switch (aMode) {
      case nsSizeMode_Minimized:
        mWindow->Minimize(true);
        break;
      case nsSizeMode_Maximized:
        if (mWindow->IsMinimized()) {
          mWindow->Minimize(false);
        }
        mWindow->Zoom();
        break;
      case nsSizeMode_Normal:
        if (mWindow->IsMinimized()) {
          mWindow->Minimize(false);
        }
        if (oldMode == nsSizeMode_Maximized && mRestoredBounds) {
          mWindow->MoveTo(mRestoredBounds->x, mRestoredBounds->y);
          mWindow->ResizeTo(mRestoredBounds->width - 1,
                            mRestoredBounds->height - 1);
        }
        break;
      default:
        break;
    }
    UpdateBounds();
    mWindow->Unlock();
  }
  mSettingSizeMode = false;

  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, mBounds.width, mBounds.height);
    mWidgetListener->SizeModeChanged(mSizeMode);
  }
}

// Full screen: hide the tab and borders, then let nsBaseWidget resize the
// window to the screen (and back to the saved bounds afterwards).
NS_IMETHODIMP nsWindow::HideWindowChrome(bool aShouldHide) {
  if (!mWindow || aShouldHide == mChromeHidden) {
    return NS_OK;
  }
  if (mWindow->Lock()) {
    if (aShouldHide) {
      mSavedLook = mWindow->Look();
      mWindow->SetLook(B_NO_BORDER_WINDOW_LOOK);
    } else {
      mWindow->SetLook(mSavedLook);
    }
    mWindow->Unlock();
  }
  mChromeHidden = aShouldHide;
  return NS_OK;
}

nsresult nsWindow::MakeFullScreen(bool aFullScreen, nsIScreen* aTargetScreen) {
  if (!mWindow) {
    return NS_ERROR_FAILURE;
  }
  if (aFullScreen && mSizeMode != nsSizeMode_Fullscreen) {
    mRestoredBounds = Some(mBounds);
  }
  // Hides/restores the chrome and resizes to the screen or the old bounds.
  nsBaseWidget::InfallibleMakeFullScreen(aFullScreen, aTargetScreen);
  if (mWindow->Lock()) {
    if (aFullScreen) {
      mWindow->Activate(true);
    }
    UpdateBounds();
    mWindow->Unlock();
  }
  mSizeMode = aFullScreen ? nsSizeMode_Fullscreen : nsSizeMode_Normal;
  if (mWidgetListener) {
    mWidgetListener->SizeModeChanged(mSizeMode);
    mWidgetListener->FullscreenChanged(aFullScreen);
  }
  SchedulePaint();
  return NS_OK;
}

nsresult nsWindow::GetRestoredBounds(LayoutDeviceIntRect& aRect) {
  if (mSizeMode == nsSizeMode_Normal || !mRestoredBounds) {
    aRect = mBounds;
  } else {
    aRect = *mRestoredBounds;
  }
  return NS_OK;
}

// Ask Gecko to repaint the whole window (main thread, coalesced).
void nsWindow::SchedulePaint() {
  if (mPaintScheduled || mOnDestroyCalled) {
    return;
  }
  mPaintScheduled = true;
  RunOnMain([](nsWindow* self) {
    self->mPaintScheduled = false;
    if (!self->mOnDestroyCalled) {
      self->Paint(LayoutDeviceIntRegion(LayoutDeviceIntRect(
          0, 0, self->mBounds.width, self->mBounds.height)));
    }
  });
}

NS_IMETHODIMP nsWindow::Invalidate(const LayoutDeviceIntRect& aRect) {
  if (!aRect.IsEmpty()) {
    SchedulePaint();
  }
  return NS_OK;
}

// Main thread. Same flow as the GTK expose handler.
void nsWindow::Paint(const LayoutDeviceIntRegion& aRegion) {
  nsIWidgetListener* listener = GetPaintListener();
  if (!listener || !mWindow || !mVisible) {
    return;
  }

  LayoutDeviceIntRegion region = aRegion;
  RefPtr<nsWindow> strongThis(this);

  ClientLayerManager* clientLayers = GetLayerManager()->AsClientLayerManager();
  if (clientLayers && mCompositorSession) {
    // Repaint even if nothing changed: the BView needs fresh pixels.
    clientLayers->SetNeedsComposite(true);
    clientLayers->SendInvalidRegion(region.ToUnknownRegion());
  }

  listener->WillPaintWindow(this);
  if (mOnDestroyCalled || !(listener = GetPaintListener())) {
    return;
  }

  if (clientLayers && clientLayers->NeedsComposite()) {
    clientLayers->Composite();
    clientLayers->SetNeedsComposite(false);
  }

  region.AndWith(LayoutDeviceIntRect(0, 0, mBounds.width, mBounds.height));
  if (region.IsEmpty()) {
    return;
  }

  if (GetLayerManager()->GetBackendType() == LayersBackend::LAYERS_CLIENT) {
    listener->PaintWindow(this, region);
    if ((listener = GetPaintListener())) {
      listener->DidPaintWindow();
    }
    return;
  }

  // Main-thread (non-OMTC) basic layers: paint into the back buffer here.
  if (GetLayerManager()->GetBackendType() == LayersBackend::LAYERS_BASIC) {
    BufferMode bufferMode = BufferMode::BUFFER_NONE;
    RefPtr<DrawTarget> dt = StartRemoteDrawingInRegion(region, &bufferMode);
    if (dt) {
      RefPtr<gfxContext> ctx = gfxContext::CreateOrNull(dt);
      if (ctx) {
        AutoLayerManagerSetup setup(this, ctx, BufferMode::BUFFER_NONE);
        listener->PaintWindow(this, region);
      }
      EndRemoteDrawingInRegion(dt, region);
    }
  }

  if ((listener = GetPaintListener())) {
    listener->DidPaintWindow();
  }
}

nsresult nsWindow::ConfigureChildren(
    const nsTArray<Configuration>& aConfigurations) {
  for (const Configuration& configuration : aConfigurations) {
    nsIWidget* w = configuration.mChild;
    if (w) {
      w->Resize(configuration.mBounds.x, configuration.mBounds.y,
                configuration.mBounds.width, configuration.mBounds.height,
                true);
    }
  }
  return NS_OK;
}

void* nsWindow::GetNativeData(uint32_t aDataType) {
  switch (aDataType) {
    case NS_NATIVE_WINDOW:
    case NS_NATIVE_SHELLWIDGET:
      return mWindow;
    case NS_NATIVE_WIDGET:
      return mView;
    case NS_RAW_NATIVE_IME_CONTEXT:
      return this;
    default:
      return nullptr;
  }
}

NS_IMETHODIMP nsWindow::SetTitle(const nsAString& aTitle) {
  if (mWindow && mWindow->Lock()) {
    mWindow->SetTitle(NS_ConvertUTF16toUTF8(aTitle).get());
    mWindow->Unlock();
  }
  return NS_OK;
}

NS_IMETHODIMP nsWindow::SetCursor(nsCursor aCursor) {
  mCursor = aCursor;
  if (mView) {
    HaikuCursor::SetCursor(mView, aCursor);
  } else if (mParent) {
    mParent->SetCursor(aCursor);
  }
  return NS_OK;
}

NS_IMETHODIMP nsWindow::SetCursor(imgIContainer* aCursor, uint32_t aHotspotX,
                                  uint32_t aHotspotY) {
  if (!mView) {
    return mParent ? mParent->SetCursor(aCursor, aHotspotX, aHotspotY)
                   : NS_ERROR_NOT_IMPLEMENTED;
  }
  if (!HaikuCursor::SetImageCursor(mView, aCursor, aHotspotX, aHotspotY,
                                   mCustomCursor)) {
    return NS_ERROR_FAILURE;
  }
  mCursor = eCursorCount;  // force SetCursor(nsCursor) to reapply
  return NS_OK;
}

LayoutDeviceIntPoint nsWindow::WidgetToScreenOffset() {
  if (!mWindow && mParent) {
    return mParent->WidgetToScreenOffset() + mBounds.TopLeft();
  }
  return mBounds.TopLeft();
}

LayoutDeviceIntRect nsWindow::GetScreenBounds() {
  return LayoutDeviceIntRect(WidgetToScreenOffset(), mBounds.Size());
}

LayoutDeviceIntRect nsWindow::GetClientBounds() { return GetScreenBounds(); }

LayoutDeviceIntPoint nsWindow::GetClientOffset() {
  return LayoutDeviceIntPoint(0, 0);
}

NS_IMETHODIMP nsWindow::DispatchEvent(WidgetGUIEvent* aEvent,
                                      nsEventStatus& aStatus) {
  aStatus = nsEventStatus_eIgnore;
  nsIWidgetListener* listener =
      mAttachedWidgetListener ? mAttachedWidgetListener : mWidgetListener;
  if (listener) {
    aStatus = listener->HandleEvent(aEvent, mUseAttachedEvents);
  }
  return NS_OK;
}

void nsWindow::CaptureRollupEvents(nsIRollupListener* aListener,
                                   bool aDoCapture) {
  if (aDoCapture) {
    gRollupListener = aListener;
  } else {
    gRollupListener = nullptr;
  }
}

// Main thread. Returns true if the mouse-down should be consumed.
bool nsWindow::RollupIfNeeded(float aX, float aY) {
  if (!gRollupListener) {
    return false;
  }
  nsCOMPtr<nsIWidget> rollupWidget = gRollupListener->GetRollupWidget();
  if (!rollupWidget || rollupWidget == this) {
    return false;
  }

  // Don't roll up if the click is inside one of the open popups.
  LayoutDeviceIntPoint screenPt =
      WidgetToScreenOffset() + LayoutDeviceIntPoint(int32_t(aX), int32_t(aY));
  AutoTArray<nsIWidget*, 5> widgetChain;
  uint32_t sameTypeCount = gRollupListener->GetSubmenuWidgetChain(&widgetChain);
  for (uint32_t i = 0; i < widgetChain.Length(); ++i) {
    if (widgetChain[i]->GetScreenBounds().Contains(screenPt)) {
      return false;
    }
  }
  (void)sameTypeCount;

  nsIntPoint pos(screenPt.x, screenPt.y);
  return gRollupListener->Rollup(UINT32_MAX, true, &pos, nullptr);
}

NS_IMETHODIMP_(void)
nsWindow::SetInputContext(const InputContext& aContext,
                          const InputContextAction& aAction) {
  mInputContext = aContext;
}

NS_IMETHODIMP_(InputContext) nsWindow::GetInputContext() {
  mInputContext.mIMEState.mOpen = IMEState::CLOSED;
  return mInputContext;
}

NS_IMETHODIMP_(TextEventDispatcherListener*)
nsWindow::GetNativeTextEventDispatcherListener() {
  return mTextListener;
}

nsIMEUpdatePreference nsWindow::GetIMEUpdatePreference() {
  // The input method asks for the caret position (B_INPUT_METHOD_LOCATION_
  // REQUEST), so keep it up to date.
  return nsIMEUpdatePreference(nsIMEUpdatePreference::NOTIFY_POSITION_CHANGE);
}

void nsWindow::OnHaikuInputMethodEvent(BMessage* aMessage) {
  if (mTextListener) {
    mTextListener->ProcessInputMethodEvent(aMessage);
  }
}

void nsWindow::SetSizeConstraints(const SizeConstraints& aConstraints) {
  nsBaseWidget::SetSizeConstraints(aConstraints);
  if (!mWindow || mIsPopup) {
    return;
  }
  if (mWindow->Lock()) {
    float minW = std::max(1, aConstraints.mMinSize.width);
    float minH = std::max(1, aConstraints.mMinSize.height);
    float maxW = std::min(32767, aConstraints.mMaxSize.width);
    float maxH = std::min(32767, aConstraints.mMaxSize.height);
    mWindow->SetSizeLimits(minW - 1, maxW - 1, minH - 1, maxH - 1);
    mWindow->Unlock();
  }
}

float nsWindow::GetDPI() { return float(96.0 * GetHaikuSystemScale()); }

double nsWindow::GetDefaultScaleInternal() { return GetHaikuSystemScale(); }

already_AddRefed<DrawTarget> nsWindow::StartRemoteDrawingInRegion(
    LayoutDeviceIntRegion& aInvalidRegion, BufferMode* aBufferMode) {
  if (!mWindow) {
    return nullptr;
  }
  *aBufferMode = BufferMode::BUFFER_NONE;
  return mBuffer.StartRemoteDrawingInRegion(mBounds.Size(), aInvalidRegion);
}

void nsWindow::EndRemoteDrawingInRegion(DrawTarget* aDrawTarget,
                                        LayoutDeviceIntRegion& aInvalidRegion) {
  mBuffer.EndRemoteDrawingInRegion(aDrawTarget, aInvalidRegion);
}

// ---------------------------------------------------------------------------
// BWindow thread callbacks. Never touch Gecko state here: post to the main
// thread.

void nsWindow::DrawToHaikuView(BView* aView, BRect aRect) {
  mBuffer.DrawToView(aView, aRect);
  if (!mBuffer.HasContent()) {
    RunOnMain([](nsWindow* self) { self->SchedulePaint(); });
  }
}

void nsWindow::OnHaikuFrameResized(int32_t aWidth, int32_t aHeight) {
  RunOnMain([aWidth, aHeight](nsWindow* self) {
    if (self->mOnDestroyCalled || !self->mWindow) {
      return;
    }
    if (self->mBounds.width == aWidth && self->mBounds.height == aHeight) {
      return;
    }
    self->mBounds.SizeTo(aWidth, aHeight);
    if (self->mWidgetListener) {
      self->mWidgetListener->WindowResized(self, aWidth, aHeight);
    }
    self->SchedulePaint();
  });
}

void nsWindow::OnHaikuFrameMoved(int32_t aX, int32_t aY) {
  RunOnMain([aX, aY](nsWindow* self) {
    if (self->mOnDestroyCalled) {
      return;
    }
    self->mBounds.MoveTo(aX, aY);
    self->NotifyWindowMoved(aX, aY);
  });
}

void nsWindow::OnHaikuZoom(bool aIsZoomed) {
  if (mSettingSizeMode) {
    return;
  }
  RunOnMain([aIsZoomed](nsWindow* self) {
    if (self->mOnDestroyCalled || !self->mWidgetListener) {
      return;
    }
    if (aIsZoomed && self->mSizeMode == nsSizeMode_Normal) {
      self->mRestoredBounds = Some(self->mBounds);
    }
    self->mSizeMode = aIsZoomed ? nsSizeMode_Maximized : nsSizeMode_Normal;
    self->mWidgetListener->SizeModeChanged(self->mSizeMode);
  });
}

void nsWindow::OnHaikuMinimize(bool aMinimize) {
  RunOnMain([aMinimize](nsWindow* self) {
    if (self->mOnDestroyCalled || !self->mWidgetListener) {
      return;
    }
    self->mSizeMode = aMinimize ? nsSizeMode_Minimized : nsSizeMode_Normal;
    self->mWidgetListener->SizeModeChanged(self->mSizeMode);
  });
}

void nsWindow::OnHaikuWindowActivated(bool aActive) {
  RunOnMain([aActive](nsWindow* self) {
    if (self->mOnDestroyCalled || !self->mWidgetListener) {
      return;
    }
    if (!aActive && !self->mIsPopup && gRollupListener) {
      gRollupListener->Rollup(UINT32_MAX, true, nullptr, nullptr);
    }
    if (!self->mWidgetListener) {
      return;
    }
    if (aActive && self->mSizeMode == nsSizeMode_Minimized) {
      // Restored from the Deskbar.
      self->mSizeMode = nsSizeMode_Normal;
      self->mWidgetListener->SizeModeChanged(nsSizeMode_Normal);
    }
    if (aActive) {
      self->mWidgetListener->WindowActivated();
    } else {
      self->mWidgetListener->WindowDeactivated();
    }
  });
}

void nsWindow::OnHaikuQuitRequested() {
  RunOnMain([](nsWindow* self) {
    if (!self->mOnDestroyCalled && self->mWidgetListener) {
      self->mWidgetListener->RequestWindowClose(self);
    }
  });
}

// Main thread.
void nsWindow::DispatchMouseEvent(EventMessage aMessage, float aX, float aY,
                                  int16_t aButton, int32_t aButtons,
                                  int32_t aClickCount, int32_t aModifiers) {
  if (mOnDestroyCalled || !mWidgetListener) {
    return;
  }
  WidgetMouseEvent event(true, aMessage, this, WidgetMouseEvent::eReal);
  event.mRefPoint = LayoutDeviceIntPoint(int32_t(aX), int32_t(aY));
  event.button = aButton;
  event.buttons = GetGeckoButtons(aButtons);
  event.mClickCount = aClickCount;
  event.mModifiers = GetGeckoModifiers(aModifiers);
  if (aMessage == eMouseExitFromWidget) {
    event.mExitFrom = WidgetMouseEvent::eTopLevel;
  }
  DispatchInputEvent(&event);
}

void nsWindow::OnHaikuMouseDown(float aX, float aY, int32_t aChangedButton,
                                int32_t aAllButtons, int32_t aClickCount,
                                int32_t aModifiers) {
  RunOnMain([aX, aY, aChangedButton, aAllButtons, aClickCount, aModifiers](nsWindow* self) {
        if (self->mOnDestroyCalled || self->RollupIfNeeded(aX, aY)) {
          return;
        }
        int16_t button = GetGeckoButton(aChangedButton);
        self->DispatchMouseEvent(eMouseDown, aX, aY, button, aAllButtons,
                                 std::max(1, aClickCount), aModifiers);
        if (button == WidgetMouseEvent::eRightButton) {
          self->DispatchMouseEvent(eContextMenu, aX, aY, button, aAllButtons,
                                   1, aModifiers);
        }
      });
}

void nsWindow::OnHaikuMouseUp(float aX, float aY, int32_t aReleasedButton,
                              int32_t aRemainingButtons, int32_t aModifiers) {
  RunOnMain([aX, aY, aReleasedButton, aRemainingButtons, aModifiers](nsWindow* self) {
        // The button went up in the window that started our outgoing drag:
        // the drag is over (dropped elsewhere, or handled by OnHaikuDrag).
        nsDragService* dragService = nsDragService::GetInstance();
        if (dragService && dragService->IsOutgoingDrag()) {
          dragService->EndDragSession(true);
        }
        self->DispatchMouseEvent(eMouseUp, aX, aY,
                                 GetGeckoButton(aReleasedButton),
                                 aRemainingButtons, 1, aModifiers);
      });
}

void nsWindow::OnHaikuMouseMoved(float aX, float aY, int32_t aButtons,
                                 int32_t aModifiers) {
  RunOnMain([aX, aY, aButtons, aModifiers](nsWindow* self) {
        self->DispatchMouseEvent(eMouseMove, aX, aY,
                                 WidgetMouseEvent::eLeftButton, aButtons, 0,
                                 aModifiers);
      });
}

void nsWindow::OnHaikuMouseCrossing(bool aEnter, float aX, float aY,
                                    int32_t aButtons, int32_t aModifiers) {
  RunOnMain([aEnter, aX, aY, aButtons, aModifiers](nsWindow* self) {
        self->DispatchMouseEvent(
            aEnter ? eMouseEnterIntoWidget : eMouseExitFromWidget, aX, aY,
            WidgetMouseEvent::eLeftButton, aButtons, 0, aModifiers);
      });
}

void nsWindow::OnHaikuWheelEvent(float aX, float aY, float aDeltaX,
                                 float aDeltaY, int32_t aModifiers) {
  RunOnMain([aX, aY, aDeltaX, aDeltaY, aModifiers](nsWindow* self) {
        if (self->mOnDestroyCalled || !self->mWidgetListener) {
          return;
        }
        WidgetWheelEvent event(true, eWheel, self);
        event.mRefPoint = LayoutDeviceIntPoint(int32_t(aX), int32_t(aY));
        event.mDeltaMode = nsIDOMWheelEvent::DOM_DELTA_LINE;
        event.mDeltaX = aDeltaX * 3.0;
        event.mDeltaY = aDeltaY * 3.0;
        event.mLineOrPageDeltaX = int32_t(aDeltaX);
        event.mLineOrPageDeltaY = int32_t(aDeltaY);
        event.mModifiers = GetGeckoModifiers(aModifiers);
        self->DispatchInputEvent(&event);
      });
}

void nsWindow::OnHaikuKeyEvent(bool aIsDown, int32_t aKey, int32_t aRawChar,
                               int32_t aModifiers, const char* aBytes,
                               int32_t aNumBytes, bool aIsRepeat) {
  HaikuKeyboard::DispatchKeyEvent(this, aIsDown, aKey, aRawChar, aModifiers,
                                  aBytes, aNumBytes, aIsRepeat);
}

void nsWindow::OnHaikuDrag(HaikuDragPhase aPhase, float aX, float aY,
                           const BMessage* aMessage, int32_t aModifiers) {
  // Copy the message: the original belongs to the BWindow thread.
  std::shared_ptr<BMessage> copy(aMessage ? new BMessage(*aMessage) : nullptr);
  RunOnMain([aPhase, aX, aY, copy, aModifiers](nsWindow* self) {
        UniquePtr<BMessage> msg(copy ? new BMessage(*copy) : nullptr);
        if (self->mOnDestroyCalled || !self->mWidgetListener) {
          return;
        }
        RefPtr<nsDragService> dragService = nsDragService::GetInstance();
        if (!dragService) {
          return;
        }
        bool ours = dragService->IsOutgoingDrag();

        nsCOMPtr<nsIDragSession> session;
        dragService->GetCurrentSession(getter_AddRefs(session));
        if (!session) {
          if (aPhase == eHaikuDragExit) {
            return;
          }
          dragService->StartDragSession();
        }
        if (!ours && msg) {
          dragService->SetDragMessage(std::move(msg));
        }

        EventMessage message = eDragOver;
        switch (aPhase) {
          case eHaikuDragEnter: message = eDragEnter; break;
          case eHaikuDragOver: message = eDragOver; break;
          case eHaikuDragExit: message = eDragExit; break;
          case eHaikuDrop: message = eDrop; break;
        }

        if (message == eDragOver) {
          dragService->FireDragEventAtSource(eDrag);
        }

        WidgetDragEvent event(true, message, self);
        event.mRefPoint = LayoutDeviceIntPoint(int32_t(aX), int32_t(aY));
        event.mModifiers = GetGeckoModifiers(aModifiers);
        self->DispatchInputEvent(&event);

        if (aPhase == eHaikuDrop || (aPhase == eHaikuDragExit && !ours)) {
          dragService->EndDragSession(aPhase == eHaikuDrop);
        }
      });
}
