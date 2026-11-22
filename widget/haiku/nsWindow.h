/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_nsWindow_h
#define mozilla_widget_nsWindow_h

#include "HaikuUtils.h"
#include "HaikuView.h"
#include "HaikuWindowBuffer.h"

#include "nsBaseWidget.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "nsThreadUtils.h"

#include <utility>

namespace mozilla::widget {
class HaikuTextInputListener;
}

/*
 * Native Haiku widget. Top-level windows, dialogs and popups each get a
 * HaikuWindow (BWindow) holding one HaikuView (BView). Child widgets
 * (plugins, remote browsers) have no native window.
 *
 * Threading: Be API callbacks run on the BWindow's thread. The OnHaiku*
 * methods marshal everything to the Gecko main thread. Painting is done by
 * the (basic) compositor into a BBitmap back buffer (HaikuWindowBuffer),
 * which is then blitted into the BView.
 */
class nsWindow final : public nsBaseWidget {
 public:
  nsWindow();

  NS_DECL_ISUPPORTS_INHERITED

  // nsIWidget
  using nsBaseWidget::Create;
  [[nodiscard]] nsresult Create(nsIWidget* aParent,
                                nsNativeWidget aNativeParent,
                                const LayoutDeviceIntRect& aRect,
                                nsWidgetInitData* aInitData) override;
  void Destroy() override;
  NS_IMETHOD Show(bool aState) override;
  bool IsVisible() const override { return mVisible; }
  NS_IMETHOD Enable(bool aState) override;
  bool IsEnabled() const override { return mEnabled; }
  NS_IMETHOD SetFocus(bool aRaise = false) override;
  void ConstrainPosition(bool aAllowSlop, int32_t* aX, int32_t* aY) override {}
  NS_IMETHOD Move(double aX, double aY) override;
  NS_IMETHOD Resize(double aWidth, double aHeight, bool aRepaint) override;
  NS_IMETHOD Resize(double aX, double aY, double aWidth, double aHeight,
                    bool aRepaint) override;
  void SetSizeMode(nsSizeMode aMode) override;
  NS_IMETHOD Invalidate(const LayoutDeviceIntRect& aRect) override;
  nsresult ConfigureChildren(
      const nsTArray<Configuration>& aConfigurations) override;
  void* GetNativeData(uint32_t aDataType) override;
  NS_IMETHOD SetTitle(const nsAString& aTitle) override;
  NS_IMETHOD SetIcon(const nsAString& aIconSpec) override { return NS_OK; }
  NS_IMETHOD SetCursor(nsCursor aCursor) override;
  NS_IMETHOD SetCursor(imgIContainer* aCursor, uint32_t aHotspotX,
                       uint32_t aHotspotY) override;
  LayoutDeviceIntPoint WidgetToScreenOffset() override;
  LayoutDeviceIntRect GetScreenBounds() override;
  LayoutDeviceIntRect GetClientBounds() override;
  LayoutDeviceIntPoint GetClientOffset() override;
  nsresult GetRestoredBounds(LayoutDeviceIntRect& aRect) override;
  NS_IMETHOD DispatchEvent(mozilla::WidgetGUIEvent* aEvent,
                           nsEventStatus& aStatus) override;
  void CaptureRollupEvents(nsIRollupListener* aListener,
                           bool aDoCapture) override;
  NS_IMETHOD_(void) SetInputContext(const InputContext& aContext,
                                    const InputContextAction& aAction) override;
  NS_IMETHOD_(InputContext) GetInputContext() override;
  NS_IMETHOD_(TextEventDispatcherListener*)
      GetNativeTextEventDispatcherListener() override;
  nsIMEUpdatePreference GetIMEUpdatePreference() override;
  NS_IMETHOD HideWindowChrome(bool aShouldHide) override;
  nsresult MakeFullScreen(bool aFullScreen,
                          nsIScreen* aTargetScreen = nullptr) override;
  void SetSizeConstraints(const SizeConstraints& aConstraints) override;
  float GetDPI() override;
  double GetDefaultScaleInternal() override;
  nsIWidget* GetParent() override { return mParent; }

  // Drawing (called on the compositor thread by the basic compositor).
  already_AddRefed<mozilla::gfx::DrawTarget> StartRemoteDrawingInRegion(
      LayoutDeviceIntRegion& aInvalidRegion,
      mozilla::layers::BufferMode* aBufferMode) override;
  void EndRemoteDrawingInRegion(mozilla::gfx::DrawTarget* aDrawTarget,
                                LayoutDeviceIntRegion& aInvalidRegion) override;

  // Posts aFunc(nsWindow*) to the main thread; it doesn't run if this
  // window has been destroyed by then. Safe to call from Be threads (it
  // doesn't AddRef the nsWindow).
  template <typename F>
  void RunOnMain(F&& aFunc) {
    RefPtr<mozilla::widget::MainThreadWeakRef<nsWindow>> ref = mWeakRef;
    if (!ref) {
      return;
    }
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        [ref, func = std::forward<F>(aFunc)]() {
          RefPtr<nsWindow> window = ref->get();
          if (window && !window->Destroyed()) {
            func(window.get());
          }
        }));
  }

  // Called on the BWindow thread.
  void DrawToHaikuView(BView* aView, BRect aRect);
  void OnHaikuFrameResized(int32_t aWidth, int32_t aHeight);
  void OnHaikuFrameMoved(int32_t aX, int32_t aY);
  void OnHaikuZoom(bool aIsZoomed);
  void OnHaikuMinimize(bool aMinimize);
  void OnHaikuWindowActivated(bool aActive);
  void OnHaikuQuitRequested();
  void OnHaikuMouseDown(float aX, float aY, int32_t aChangedButton,
                        int32_t aAllButtons, int32_t aClickCount,
                        int32_t aModifiers);
  void OnHaikuMouseUp(float aX, float aY, int32_t aReleasedButton,
                      int32_t aRemainingButtons, int32_t aModifiers);
  void OnHaikuMouseMoved(float aX, float aY, int32_t aButtons,
                         int32_t aModifiers);
  void OnHaikuMouseCrossing(bool aEnter, float aX, float aY, int32_t aButtons,
                            int32_t aModifiers);
  void OnHaikuWheelEvent(float aX, float aY, float aDeltaX, float aDeltaY,
                         int32_t aModifiers);
  enum HaikuDragPhase {
    eHaikuDragEnter,
    eHaikuDragOver,
    eHaikuDragExit,
    eHaikuDrop
  };
  void OnHaikuDrag(HaikuDragPhase aPhase, float aX, float aY,
                   const BMessage* aMessage, int32_t aModifiers);
  void OnHaikuInputMethodEvent(BMessage* aMessage);
  void OnHaikuKeyEvent(bool aIsDown, int32_t aKey, int32_t aRawChar,
                       int32_t aModifiers, const char* aBytes,
                       int32_t aNumBytes, bool aIsRepeat);

 protected:
  ~nsWindow() override;

 private:
  nsIWidgetListener* GetPaintListener();
  void UpdateBounds();
  void Paint(const LayoutDeviceIntRegion& aRegion);
  void SchedulePaint();
  void DispatchMouseEvent(mozilla::EventMessage aMessage, float aX, float aY,
                          int16_t aButton, int32_t aButtons,
                          int32_t aClickCount, int32_t aModifiers);
  bool RollupIfNeeded(float aX, float aY);

  HaikuWindow* mWindow;
  HaikuView* mView;
  nsCOMPtr<nsIWidget> mParent;
  mozilla::widget::HaikuWindowBuffer mBuffer;
  RefPtr<mozilla::widget::HaikuTextInputListener> mTextListener;
  RefPtr<mozilla::widget::MainThreadWeakRef<nsWindow>> mWeakRef;

  mozilla::Maybe<LayoutDeviceIntRect> mRestoredBounds;
  InputContext mInputContext;
  BRect mSavedFrame;
  window_look mSavedLook = B_TITLED_WINDOW_LOOK;
  bool mChromeHidden = false;
  mozilla::UniquePtr<BCursor> mCustomCursor;

  bool mVisible;
  bool mEnabled;
  bool mIsPopup;
  bool mSettingSizeMode;
  bool mPaintScheduled;
};

#endif  // mozilla_widget_nsWindow_h
