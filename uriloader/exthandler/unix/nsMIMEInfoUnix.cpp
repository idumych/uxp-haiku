/* -*- Mode: C++; tab-width: 3; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMIMEInfoUnix.h"
#include "nsGNOMERegistry.h"
#include "nsIGIOService.h"
#include "nsNetCID.h"
#include "nsIIOService.h"
#include "nsAutoPtr.h"
#ifdef MOZ_ENABLE_DBUS
#include "nsDBusHandlerApp.h"
#endif
#if defined(XP_HAIKU)
#include <spawn.h>
#include <sys/wait.h>
#pragma GCC visibility push(default)
#include <Mime.h>
#pragma GCC visibility pop
#include "nsIFile.h"

extern char** environ;

// Whether Haiku has a preferred application for this MIME type.
bool
nsMIMEInfoUnix::HaikuHasPreferredApp(const nsACString& aType)
{
  BMimeType mime(PromiseFlatCString(aType).get());
  char app[B_MIME_TYPE_LENGTH];
  return mime.InitCheck() == B_OK && mime.GetPreferredApp(app) == B_OK &&
         app[0] != '\0';
}

// Haiku has no GIO: hand URIs and files to /bin/open, which launches the
// application registered for the scheme or file type.
static nsresult
HaikuOpen(const nsACString& aTarget)
{
  nsAutoCString target(aTarget);
  char openPath[] = "/bin/open";
  char* argv[] = { openPath, target.BeginWriting(), nullptr };
  pid_t pid;
  if (posix_spawn(&pid, openPath, nullptr, nullptr, argv, environ) != 0)
    return NS_ERROR_FAILURE;
  int status = 0;
  if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0)
    return NS_ERROR_FAILURE;
  return NS_OK;
}
#endif

nsresult
nsMIMEInfoUnix::LoadUriInternal(nsIURI * aURI)
{
#if defined(XP_HAIKU)
  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);
  return HaikuOpen(spec);
#else
  return nsGNOMERegistry::LoadURL(aURI);
#endif
}

NS_IMETHODIMP
nsMIMEInfoUnix::GetHasDefaultHandler(bool *_retval)
{
  // if mDefaultApplication is set, it means the application has been set from
  // either /etc/mailcap or ${HOME}/.mailcap, in which case we don't want to
  // give the GNOME answer.
  if (mDefaultApplication)
    return nsMIMEInfoImpl::GetHasDefaultHandler(_retval);

  *_retval = false;

#if defined(XP_HAIKU)
  if (mClass == eProtocolInfo) {
    *_retval = HaikuHasPreferredApp(
      NS_LITERAL_CSTRING("application/x-vnd.Be.URL.") + mSchemeOrType);
  } else {
    *_retval = HaikuHasPreferredApp(mSchemeOrType);
  }
  return NS_OK;
#endif

  if (mClass == eProtocolInfo) {
    *_retval = nsGNOMERegistry::HandlerExists(mSchemeOrType.get());
  } else {
    RefPtr<nsMIMEInfoBase> mimeInfo = nsGNOMERegistry::GetFromType(mSchemeOrType);
    if (!mimeInfo) {
      nsAutoCString ext;
      nsresult rv = GetPrimaryExtension(ext);
      if (NS_SUCCEEDED(rv)) {
        mimeInfo = nsGNOMERegistry::GetFromExtension(ext);
      }
    }
    if (mimeInfo)
      *_retval = true;
  }

  if (*_retval)
    return NS_OK;

#if defined(MOZ_ENABLE_CONTENTACTION)
  ContentAction::Action action = 
    ContentAction::Action::defaultActionForFile(QUrl(), QString(mSchemeOrType.get()));
  if (action.isValid()) {
    *_retval = true;
    return NS_OK;
  }
#endif

  return NS_OK;
}

nsresult
nsMIMEInfoUnix::LaunchDefaultWithFile(nsIFile *aFile)
{
  // if mDefaultApplication is set, it means the application has been set from
  // either /etc/mailcap or ${HOME}/.mailcap, in which case we don't want to
  // give the GNOME answer.
  if (mDefaultApplication)
    return nsMIMEInfoImpl::LaunchDefaultWithFile(aFile);

  nsAutoCString nativePath;
  aFile->GetNativePath(nativePath);

#if defined(XP_HAIKU)
  return HaikuOpen(nativePath);
#endif

#if defined(MOZ_ENABLE_CONTENTACTION)
  QUrl uri = QUrl::fromLocalFile(QString::fromUtf8(nativePath.get()));
  ContentAction::Action action =
    ContentAction::Action::defaultActionForFile(uri, QString(mSchemeOrType.get()));
  if (action.isValid()) {
    action.trigger();
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
#endif

  nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
  if (!giovfs) {
    return NS_ERROR_FAILURE;
  }

  // nsGIOMimeApp->Launch wants a URI string instead of local file
  nsresult rv;
  nsCOMPtr<nsIIOService> ioservice = do_GetService(NS_IOSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIURI> uri;
  rv = ioservice->NewFileURI(aFile, getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString uriSpec;
  uri->GetSpec(uriSpec);

  nsCOMPtr<nsIGIOMimeApp> app;
  if (NS_FAILED(giovfs->GetAppForMimeType(mSchemeOrType, getter_AddRefs(app))) || !app) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  return app->Launch(uriSpec);
}

#if defined(MOZ_ENABLE_CONTENTACTION)
NS_IMETHODIMP
nsMIMEInfoUnix::GetPossibleApplicationHandlers(nsIMutableArray ** aPossibleAppHandlers)
{
  if (!mPossibleApplications) {
    mPossibleApplications = do_CreateInstance(NS_ARRAY_CONTRACTID);

    if (!mPossibleApplications)
      return NS_ERROR_OUT_OF_MEMORY;

    QList<ContentAction::Action> actions =
      ContentAction::Action::actionsForFile(QUrl(), QString(mSchemeOrType.get()));

    for (int i = 0; i < actions.size(); ++i) {
      nsContentHandlerApp* app =
        new nsContentHandlerApp(nsString((char16_t*)actions[i].name().data()), 
                                mSchemeOrType, actions[i]);
      mPossibleApplications->AppendElement(app, false);
    }
  }

  *aPossibleAppHandlers = mPossibleApplications;
  NS_ADDREF(*aPossibleAppHandlers);
  return NS_OK;
}
#endif

