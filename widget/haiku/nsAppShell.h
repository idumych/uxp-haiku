/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_nsAppShell_h
#define mozilla_widget_nsAppShell_h

#include "nsBaseAppShell.h"
#include "HaikuUtils.h"
#include "mozilla/Atomics.h"
#include "nsTArray.h"
#include "nsString.h"

/*
 * The Gecko main thread is not the BApplication thread: BApplication runs
 * its own looper thread (HaikuApplication), and every BWindow has its own
 * thread too. Native events are forwarded to the main thread with
 * NS_DispatchToMainThread; the main thread sleeps on a port that
 * ScheduleNativeEventCallback writes to.
 */
class nsAppShell : public nsBaseAppShell {
 public:
  nsAppShell();

  nsresult Init();

  NS_IMETHOD Run(void) override;
  NS_IMETHOD Exit(void) override;

  void ScheduleQuitEvent();

  static void DispatchThemeChanged();
  static void DispatchArgv(nsTArray<nsCString>&& aArgs);
  static void DispatchQuit(sem_id aCheckSem, bool* aAllowedQuit);

 protected:
  ~nsAppShell() override;

  void ScheduleNativeEventCallback() override;
  bool ProcessNextNativeEvent(bool aMayWait) override;

 private:
  static void TermSignalHandler(int aSigno);
  void InstallTermSignalHandler();

  static nsAppShell* sHaikuAppShell;

  port_id mWakeupPort;
  mozilla::Atomic<bool> mWakeupPending;
};

#endif  // mozilla_widget_nsAppShell_h
