/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAppShell.h"
#include "HaikuApplication.h"

#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <utility>

#include "mozilla/HangMonitor.h"
#include "mozilla/LookAndFeel.h"
#include "nsXULAppAPI.h"
#include "nsIAppStartup.h"
#include "nsICommandLineRunner.h"
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIObserverService.h"
#include "nsThreadUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsComponentManagerUtils.h"

#include "mozilla/Services.h"

using namespace mozilla;
using namespace mozilla::widget;

static const int32 kGeckoWakeupMsg = 'GWKP';
static const int32 kGeckoQuitMsg = 'GQIT';

nsAppShell* nsAppShell::sHaikuAppShell = nullptr;

nsAppShell::nsAppShell() : mWakeupPort(-1), mWakeupPending(false) {
  sHaikuAppShell = this;
}

nsAppShell::~nsAppShell() {
  sHaikuAppShell = nullptr;

  if (mWakeupPort >= B_OK) {
    delete_port(mWakeupPort);
    mWakeupPort = -1;
  }
}

void nsAppShell::TermSignalHandler(int aSigno) {
  if (aSigno == SIGTERM && sHaikuAppShell) {
    sHaikuAppShell->ScheduleQuitEvent();
  }
}

void nsAppShell::InstallTermSignalHandler() {
  if (!XRE_IsParentProcess() || getenv("MOZ_DISABLE_SIG_HANDLER")) {
    return;
  }

  struct sigaction act = {}, oldact;
  act.sa_handler = TermSignalHandler;
  sigfillset(&act.sa_mask);

  if (sigaction(SIGTERM, nullptr, &oldact) != 0) {
    return;
  }
  if (oldact.sa_handler != SIG_DFL) {
    NS_WARNING("SIGTERM signal handler is already set");
  }
  sigaction(SIGTERM, &act, nullptr);
}

nsresult nsAppShell::Init() {
  HaikuApplication::EnsureInitialized();

  InstallTermSignalHandler();

  mWakeupPort = create_port(100, "gecko_appshell_wakeup");

  return nsBaseAppShell::Init();
}

void nsAppShell::ScheduleNativeEventCallback() {
  if (mWakeupPort < B_OK) {
    return;
  }
  if (!mWakeupPending.exchange(true)) {
    if (write_port_etc(mWakeupPort, kGeckoWakeupMsg, nullptr, 0, B_TIMEOUT,
                       0) != B_OK) {
      mWakeupPending = false;
    }
  }
}

void nsAppShell::ScheduleQuitEvent() {
  if (mWakeupPort >= B_OK) {
    write_port_etc(mWakeupPort, kGeckoQuitMsg, nullptr, 0, B_TIMEOUT, 0);
  }
}

bool nsAppShell::ProcessNextNativeEvent(bool aMayWait) {
  if (mWakeupPort < B_OK) {
    return false;
  }

  int32 msgCode = 0;
  status_t res;

  if (aMayWait) {
    mozilla::HangMonitor::Suspend();
    res = read_port(mWakeupPort, &msgCode, nullptr, 0);
    mozilla::HangMonitor::NotifyActivity();
  } else {
    res = read_port_etc(mWakeupPort, &msgCode, nullptr, 0, B_TIMEOUT, 0);
  }

  if (res < B_OK) {
    return false;
  }

  mWakeupPending = false;
  bool quit = (msgCode == kGeckoQuitMsg);

  while (port_count(mWakeupPort) > 0) {
    int32 extraMsg = 0;
    read_port_etc(mWakeupPort, &extraMsg, nullptr, 0, B_TIMEOUT, 0);
    quit = quit || (extraMsg == kGeckoQuitMsg);
  }

  if (quit) {
    nsCOMPtr<nsIAppStartup> appStartup =
        do_GetService("@mozilla.org/toolkit/app-startup;1");
    if (appStartup) {
      appStartup->Quit(nsIAppStartup::eForceQuit);
    }
    return true;
  }

  NativeEventCallback();
  return true;
}

NS_IMETHODIMP nsAppShell::Run(void) {
  nsresult rv = nsBaseAppShell::Run();
  HaikuApplication::NotifyGeckoExited();
  return rv;
}

NS_IMETHODIMP nsAppShell::Exit(void) {
  ScheduleNativeEventCallback();
  return nsBaseAppShell::Exit();
}

void nsAppShell::DispatchThemeChanged() {
  NS_DispatchToMainThread(NS_NewRunnableFunction([]() {
    LookAndFeel::Refresh();
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->NotifyObservers(nullptr, "look-and-feel-pref-changed", nullptr);
    }
  }));
}

void nsAppShell::DispatchArgv(nsTArray<nsCString>&& aArgs) {
  NS_DispatchToMainThread(
      NS_NewRunnableFunction([args = std::move(aArgs)]() {
        nsCOMPtr<nsICommandLineRunner> cmdLine =
            do_CreateInstance("@mozilla.org/toolkit/command-line;1");
        if (!cmdLine) {
          return;
        }

        AutoTArray<char*, 16> argv;
        for (const auto& arg : args) {
          argv.AppendElement(const_cast<char*>(arg.get()));
        }

        nsCOMPtr<nsIFile> workingDir;
        NS_GetSpecialDirectory(NS_OS_CURRENT_WORKING_DIR,
                               getter_AddRefs(workingDir));

        if (NS_SUCCEEDED(cmdLine->Init(argv.Length(), argv.Elements(),
                                       workingDir,
                                       nsICommandLine::STATE_REMOTE_EXPLICIT))) {
          cmdLine->Run();
        }
      }));
}

void nsAppShell::DispatchQuit(sem_id aCheckSem, bool* aAllowedQuit) {
  NS_DispatchToMainThread(NS_NewRunnableFunction([aCheckSem, aAllowedQuit]() {
    nsCOMPtr<nsIAppStartup> appStartup =
        do_GetService("@mozilla.org/toolkit/app-startup;1");
    if (appStartup) {
      appStartup->Quit(nsIAppStartup::eAttemptQuit);
      *aAllowedQuit = true;
    }
    if (aCheckSem >= B_OK) {
      release_sem(aCheckSem);
    }
  }));
}
