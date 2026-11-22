/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_HaikuApplication_h
#define mozilla_widget_HaikuApplication_h

#include "HaikuUtils.h"

namespace mozilla::widget {

// The BApplication runs on its own thread; the Gecko main thread keeps
// running nsAppShell. See nsAppShell.h.
class HaikuApplication final : public BApplication {
 public:
  static void EnsureInitialized();
  static void Shutdown();
  static void NotifyGeckoExited();

  explicit HaikuApplication(const char* aSignature);
  ~HaikuApplication() override;

  void ReadyToRun() override;
  void MessageReceived(BMessage* aMessage) override;
  void RefsReceived(BMessage* aMessage) override;
  void ArgvReceived(int32 aArgc, char** aArgv) override;
  bool QuitRequested() override;

 private:
  static int32 AppThreadFunc(void* aData);

  static HaikuApplication* sInstance;
  static sem_id sGeckoTerminatedSem;
  static sem_id sWidgetsShutDownSem;
  static bool sAppAwaitsShutdown;
  static bool sGeckoShutDown;
  thread_id mThread;
};

}  // namespace mozilla::widget

#endif  // mozilla_widget_HaikuApplication_h
