/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuApplication.h"
#include "nsAppShell.h"
#include "HaikuRemote.h"

#include <cstdio>
#include <kernel/image.h>
#include <storage/AppFileInfo.h>
#include <storage/File.h>
#include <storage/Resources.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <strings.h>
#include <unistd.h>
#include <utility>

#include "nsString.h"
#include "nsTArray.h"
#include "nsXULAppAPI.h"

namespace mozilla::widget {

HaikuApplication* HaikuApplication::sInstance = nullptr;
sem_id HaikuApplication::sGeckoTerminatedSem = B_BAD_SEM_ID;

// This instance's signature (see HaikuForwardToRunningInstance).
static char sSignature[B_MIME_TYPE_LENGTH];

// The executable's own resources: app_signature and the UXP:APP_NAME string.
static void ReadAppIdentity(char* aSignature, char* aName, size_t aNameSize) {
  strlcpy(aSignature, "application/x-vnd.UXP-browser", B_MIME_TYPE_LENGTH);
  strlcpy(aName, "Web browser", aNameSize);
  image_info info;
  int32 cookie = 0;
  while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
    if (info.type != B_APP_IMAGE) {
      continue;
    }
    BFile file(info.name, B_READ_ONLY);
    BAppFileInfo appInfo(&file);
    if (appInfo.InitCheck() == B_OK) {
      appInfo.GetSignature(aSignature);
    }
    BResources resources(&file);
    size_t size = 0;
    const void* data = resources.LoadResource(B_STRING_TYPE, "UXP:APP_NAME",
                                              &size);
    if (data && size > 0) {
      strlcpy(aName, static_cast<const char*>(data),
              size < aNameSize ? size + 1 : aNameSize);
    }
    break;
  }
}

static char sBaseSignature[B_MIME_TYPE_LENGTH];
static char sAppName[64];

static void EnsureAppIdentity() {
  if (!sBaseSignature[0]) {
    ReadAppIdentity(sBaseSignature, sAppName, sizeof(sAppName));
    if (!sSignature[0]) {
      strlcpy(sSignature, sBaseSignature, sizeof(sSignature));
    }
  }
}

const char* HaikuAppSignature() {
  EnsureAppIdentity();
  return sBaseSignature;
}

const char* HaikuAppName() {
  EnsureAppIdentity();
  return sAppName;
}
static sem_id sInitSem = B_BAD_SEM_ID;
// Set once ReadyToRun has run; argv messages before that are our own.
static bool sReady = false;

static const char* ProfileArg(int aArgc, char** aArgv) {
  for (int i = 1; i < aArgc - 1; ++i) {
    const char* arg = aArgv[i];
    while (*arg == '-') {
      ++arg;
    }
    if (!strcmp(arg, "P") || !strcasecmp(arg, "profile")) {
      return aArgv[i + 1];
    }
  }
  return nullptr;
}

static bool HasArg(int aArgc, char** aArgv, const char* aName) {
  for (int i = 1; i < aArgc; ++i) {
    const char* arg = aArgv[i];
    while (*arg == '-') {
      ++arg;
    }
    if (!strcasecmp(arg, aName)) {
      return true;
    }
  }
  return false;
}

bool HaikuForwardToRunningInstance(int aArgc, char** aArgv) {
  EnsureAppIdentity();
  const char* profile = ProfileArg(aArgc, aArgv);
  const char* noRemote = getenv("MOZ_NO_REMOTE");
  const char* newInstance = getenv("MOZ_NEW_INSTANCE");
  bool separate = (noRemote && *noRemote) || (newInstance && *newInstance) ||
                  HasArg(aArgc, aArgv, "no-remote") ||
                  HasArg(aArgc, aArgv, "new-instance");

  if (profile) {
    // One instance per profile: hash the profile name/path.
    uint32_t hash = 5381;
    for (const char* c = profile; *c; ++c) {
      hash = hash * 33 + uint8_t(*c);
    }
    snprintf(sSignature, sizeof(sSignature), "%s-%08x", HaikuAppSignature(),
             hash);
  } else if (separate) {
    // Never shares: a signature nobody else uses.
    snprintf(sSignature, sizeof(sSignature), "%s-%d", HaikuAppSignature(),
             int(getpid()));
    return false;
  } else if (HasArg(aArgc, aArgv, "ProfileManager")) {
    return false;
  }

  if (separate && profile) {
    // -no-remote -P name: still don't hand off to another instance.
    return false;
  }

  team_id team = be_roster->TeamFor(sSignature);
  if (team < B_OK || team == team_id(getpid())) {
    return false;
  }

  BMessage msg(B_ARGV_RECEIVED);
  msg.AddInt32("argc", aArgc);
  for (int i = 0; i < aArgc; ++i) {
    msg.AddString("argv", aArgv[i]);
  }
  char cwd[B_PATH_NAME_LENGTH];
  if (getcwd(cwd, sizeof(cwd))) {
    msg.AddString("cwd", cwd);
  }
  BMessenger target(sSignature, team);
  if (target.SendMessage(&msg, (BHandler*)nullptr, 5000000) != B_OK) {
    return false;  // couldn't reach it: start normally
  }
  be_roster->ActivateApp(team);
  return true;
}

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
  EnsureAppIdentity();
  auto* app = new HaikuApplication(sSignature);
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
  sReady = true;
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
  // Before ReadyToRun this is our own command line, which XRE handles.
  // Afterwards it comes from another launch (HaikuForwardToRunningInstance
  // or BRoster::Launch by another app): open it here.
  if (!sReady || aArgc <= 1 || !aArgv) {
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
