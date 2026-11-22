/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuApplication.h"
#include "nsAppShell.h"

#include <mutex>
#include <utility>

#include "nsString.h"
#include "nsTArray.h"
#include "nsXULAppAPI.h"

namespace mozilla::widget {

HaikuApplication* HaikuApplication::sInstance = nullptr;
sem_id HaikuApplication::sGeckoTerminatedSem = B_BAD_SEM_ID;

static const char* kAppSignature = "application/x-vnd.Basilisk";
static sem_id sInitSem = B_BAD_SEM_ID;

// Starts the BApplication thread once and waits until it is running
// (ReadyToRun), so be_app, be_plain_font, be_control_look and ui_color() are
// usable. Thread-safe; called early from the app shell, look and feel, the
// theme, screens and windows.
void HaikuApplication::EnsureInitialized() {
  static std::once_flag sOnce;
  std::call_once(sOnce, []() {
    if (be_app || !XRE_IsParentProcess()) {
      return;
    }
    sInitSem = create_sem(0, "gecko_app_init_sem");
    thread_id thread = spawn_thread(AppThreadFunc, "BApplication_thread",
                                    B_NORMAL_PRIORITY, nullptr);
    if (thread >= B_OK) {
      resume_thread(thread);
      acquire_sem_etc(sInitSem, 1, B_RELATIVE_TIMEOUT, 10000000);
    }
    delete_sem(sInitSem);
    sInitSem = B_BAD_SEM_ID;
  });
}

void HaikuApplication::Shutdown() {
  if (!sInstance) {
    return;
  }
  thread_id appThread = sInstance->mThread;
  if (sInstance->Lock()) {
    sInstance->Quit();
  }
  if (appThread >= B_OK) {
    status_t status;
    wait_for_thread(appThread, &status);
  }
}

void HaikuApplication::NotifyGeckoExited() {
  if (sGeckoTerminatedSem >= B_OK) {
    release_sem(sGeckoTerminatedSem);
  }
}

int32 HaikuApplication::AppThreadFunc(void*) {
  auto* app = new HaikuApplication(kAppSignature);
  sInstance = app;
  app->mThread = find_thread(nullptr);
  app->Run();
  delete app;
  sInstance = nullptr;
  return B_OK;
}

HaikuApplication::HaikuApplication(const char* aSignature)
    : BApplication(aSignature), mThread(-1) {}

HaikuApplication::~HaikuApplication() = default;

void HaikuApplication::ReadyToRun() {
  if (sInitSem >= B_OK) {
    release_sem(sInitSem);
  }
}

void HaikuApplication::MessageReceived(BMessage* aMessage) {
  switch (aMessage->what) {
    case B_COLORS_UPDATED:
      nsAppShell::DispatchThemeChanged();
      break;
    default:
      BApplication::MessageReceived(aMessage);
      break;
  }
}

void HaikuApplication::RefsReceived(BMessage* aMessage) {
  nsTArray<nsCString> args;
  args.AppendElement(NS_LITERAL_CSTRING("basilisk"));

  entry_ref ref;
  for (int32 i = 0; aMessage->FindRef("refs", i, &ref) == B_OK; ++i) {
    // Bookmark files carry the URL in an attribute.
    BNode node(&ref);
    char urlBuf[2048];
    ssize_t bytes = node.InitCheck() == B_OK
        ? node.ReadAttr("META:url", B_STRING_TYPE, 0, urlBuf,
                        sizeof(urlBuf) - 1)
        : -1;
    args.AppendElement(NS_LITERAL_CSTRING("-url"));
    if (bytes > 0) {
      urlBuf[bytes] = '\0';
      args.AppendElement(nsCString(urlBuf));
      continue;
    }
    BPath path(&ref);
    if (path.InitCheck() == B_OK) {
      args.AppendElement(nsCString(path.Path()));
    } else {
      args.RemoveElementAt(args.Length() - 1);
    }
  }

  if (args.Length() > 1) {
    nsAppShell::DispatchArgv(std::move(args));
  }
}

void HaikuApplication::ArgvReceived(int32 aArgc, char** aArgv) {
  // The first ArgvReceived is our own command line, which XRE handles.
  static bool sInitialArgvHandled = false;
  if (!sInitialArgvHandled) {
    sInitialArgvHandled = true;
    return;
  }
  if (aArgc <= 1 || !aArgv) {
    return;
  }

  nsTArray<nsCString> args;
  for (int i = 0; i < aArgc; ++i) {
    args.AppendElement(nsCString(aArgv[i]));
  }
  nsAppShell::DispatchArgv(std::move(args));
}

bool HaikuApplication::QuitRequested() {
  if (!XRE_IsParentProcess()) {
    return true;
  }

  // Ask Gecko to quit on the main thread, then wait for it to finish.
  sGeckoTerminatedSem = create_sem(0, "gecko_terminated_sem");
  sem_id checkSem = create_sem(0, "gecko_quit_check_sem");
  if (sGeckoTerminatedSem < B_OK || checkSem < B_OK) {
    return false;
  }

  bool allowedQuit = false;
  nsAppShell::DispatchQuit(checkSem, &allowedQuit);
  status_t checkRes = acquire_sem_etc(checkSem, 1, B_RELATIVE_TIMEOUT, 5000000);
  delete_sem(checkSem);

  status_t termRes = B_ERROR;
  if (checkRes == B_OK && allowedQuit) {
    termRes = acquire_sem_etc(sGeckoTerminatedSem, 1, B_RELATIVE_TIMEOUT,
                              5000000);
  }
  delete_sem(sGeckoTerminatedSem);
  sGeckoTerminatedSem = B_BAD_SEM_ID;
  return termRes == B_OK;
}

}  // namespace mozilla::widget
