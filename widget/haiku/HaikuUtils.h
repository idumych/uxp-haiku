/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuUtils_h
#define mozilla_widget_HaikuUtils_h

#include <cstdint>

#pragma GCC visibility push(default)

#include <app/Application.h>
#include <app/Clipboard.h>
#include <app/Cursor.h>
#include <app/Looper.h>
#include <app/Message.h>
#include <app/MessageRunner.h>
#include <app/Messenger.h>
#include <app/Notification.h>
#include <app/Roster.h>
#include <game/WindowScreen.h>
#include <interface/Bitmap.h>
#include <interface/Font.h>
#include <interface/Input.h>
#include <interface/InterfaceDefs.h>
#include <interface/Rect.h>
#include <interface/Screen.h>
#include <interface/View.h>
#include <interface/Window.h>
#include <kernel/fs_attr.h>
#include <kernel/OS.h>
#include <storage/Directory.h>
#include <storage/Entry.h>
#include <storage/FilePanel.h>
#include <storage/Mime.h>
#include <storage/Node.h>
#include <storage/NodeInfo.h>
#include <storage/Path.h>
#include <support/Beep.h>
#include <support/Archivable.h>
#include <support/DataIO.h>
#include <translation/BitmapStream.h>
#include <translation/TranslationDefs.h>
#include <translation/TranslatorRoster.h>

#pragma GCC visibility pop

#include "nsISupportsImpl.h"

class imgIContainer;

namespace mozilla::gfx {
class SourceSurface;
}

namespace mozilla::widget {

// A weak, thread-safe handle to a main-thread object. Be API threads
// (BWindow, BLooper) may copy it and post runnables holding it; the pointer
// itself is only set, cleared and dereferenced on the main thread, so a
// runnable that runs after the object is gone just sees null. This avoids
// AddRef'ing main-thread-only XPCOM objects from Be threads.
template <class T>
class MainThreadWeakRef final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MainThreadWeakRef)
  explicit MainThreadWeakRef(T* aPtr) : mPtr(aPtr) {}
  T* get() const { return mPtr; }    // main thread only
  void Clear() { mPtr = nullptr; }  // main thread only

 private:
  ~MainThreadWeakRef() = default;
  T* mPtr;
};

void EnsureHaikuApplication();
double GetHaikuSystemScale();
uint32_t GetHaikuPanelColorPixel();

BBitmap* CreateBBitmapFromSurface(
    mozilla::gfx::SourceSurface* aSurface,
    float aAlphaMultiplier = 1.0f,
    int32_t aMaxWidth = -1,
    int32_t aMaxHeight = -1);

// Encodes aBitmap as PNG with the Translation Kit. Takes ownership of
// aBitmap. Returns false on failure.
bool EncodeBitmapAsPNG(BBitmap* aBitmap, BMallocIO& aOut);

BBitmap* CreateBBitmapFromImage(
    imgIContainer* aImage,
    float aAlphaMultiplier = 1.0f,
    int32_t aMaxWidth = -1,
    int32_t aMaxHeight = -1);

}  // namespace mozilla::widget

#endif  // mozilla_widget_HaikuUtils_h
