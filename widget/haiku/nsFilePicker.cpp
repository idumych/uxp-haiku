/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* File picker on top of BFilePanel. The panel replies to a private BLooper,
 * which forwards the result to the main thread. Show() (the deprecated
 * synchronous API, also used by nsBaseFilePicker::Open's default runnable)
 * spins the Gecko event loop until the panel is closed. */

#include "nsFilePicker.h"

#include "HaikuFilePanel.h"
#include "HaikuUtils.h"
#include "nsArrayEnumerator.h"
#include "nsIWidget.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"

using namespace mozilla;

namespace mozilla {
namespace widget {

// Called by the file panel looper (HaikuFilePanel.cpp) on its own thread.
// Lives here because that file is built with RTTI, and a runnable there would
// need typeinfo for mozilla::Runnable, which Gecko (built without RTTI) lacks.
void DeliverFilePanelResult(MainThreadWeakRef<nsFilePicker>* aRef,
                            int16_t aResult, nsTArray<nsCString>&& aPaths)
{
  // Don't AddRef the picker on this thread; resolve it on the main thread.
  RefPtr<MainThreadWeakRef<nsFilePicker>> ref = aRef;
  nsTArray<nsCString> paths(std::move(aPaths));
  NS_DispatchToMainThread(NS_NewRunnableFunction([ref, aResult, paths]() {
    RefPtr<nsFilePicker> picker = ref->get();
    if (picker) {
      picker->OnPanelClosed(aResult, paths);
    }
  }));
}

} // namespace widget
} // namespace mozilla

NS_IMPL_ISUPPORTS(nsFilePicker, nsIFilePicker)

nsFilePicker::nsFilePicker()
  : mSelectedTypeIndex(0)
  , mResult(nsIFilePicker::returnCancel)
  , mRunning(false)
  , mPanel(nullptr)
  , mLooper(nullptr)
  , mRefFilter(nullptr)
{
}

nsFilePicker::~nsFilePicker()
{
  ClosePanel();
}

void
nsFilePicker::ClosePanel()
{
  delete mPanel;
  mPanel = nullptr;
  delete mRefFilter;
  mRefFilter = nullptr;
  if (mLooper) {
    mozilla::widget::DetachFilePanelLooper(mLooper);
    if (mLooper->Lock()) {
      mLooper->Quit();  // deletes the looper
    }
    mLooper = nullptr;
  }
}

void
nsFilePicker::InitNative(nsIWidget* aParent, const nsAString& aTitle)
{
  mParentWidget = aParent;
  mTitle = aTitle;
}

nsresult
nsFilePicker::ShowPanel()
{
  ClosePanel();
  mozilla::widget::EnsureHaikuApplication();
  if (!be_app) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  file_panel_mode panelMode = B_OPEN_PANEL;
  uint32 nodeFlavors = B_FILE_NODE | B_SYMLINK_NODE;
  bool allowMultiple = false;

  switch (mMode) {
    case modeOpen:
      break;
    case modeOpenMultiple:
      allowMultiple = true;
      break;
    case modeGetFolder:
      nodeFlavors = B_DIRECTORY_NODE;
      break;
    case modeSave:
      panelMode = B_SAVE_PANEL;
      break;
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }

  mLooper = mozilla::widget::CreateFilePanelLooper(this);
  mLooper->Run();
  BMessenger target(mLooper);

  mPanel = new BFilePanel(panelMode, &target, nullptr, nodeFlavors,
                          allowMultiple, nullptr, nullptr, true, true);

  if (mDisplayDirectory) {
    nsAutoCString dirPath;
    if (NS_SUCCEEDED(mDisplayDirectory->GetNativePath(dirPath))) {
      mPanel->SetPanelDirectory(dirPath.get());
    }
  }

  // Apply the selected nsIFilePicker filter (open modes only; a save panel
  // must show everything in the folder).
  if (mMode != modeSave && mMode != modeGetFolder && !mFilters.IsEmpty()) {
    int32_t index = mSelectedTypeIndex;
    if (index < 0 || index >= int32_t(mFilters.Length())) {
      index = 0;
    }
    mRefFilter = mozilla::widget::CreatePatternRefFilter(mFilters[index]);
    mPanel->SetRefFilter(mRefFilter);
  }

  if (mMode == modeSave && !mDefaultFilename.IsEmpty()) {
    mPanel->SetSaveText(NS_ConvertUTF16toUTF8(mDefaultFilename).get());
  }

  if (!mOkButtonLabel.IsEmpty()) {
    mPanel->SetButtonLabel(B_DEFAULT_BUTTON,
                           NS_ConvertUTF16toUTF8(mOkButtonLabel).get());
  }

  BWindow* win = mPanel->Window();
  if (win && win->Lock()) {
    if (!mTitle.IsEmpty()) {
      win->SetTitle(NS_ConvertUTF16toUTF8(mTitle).get());
    }
    BWindow* parentWin = mParentWidget
      ? static_cast<BWindow*>(mParentWidget->GetNativeData(NS_NATIVE_WINDOW))
      : nullptr;
    if (parentWin && parentWin->LockWithTimeout(10000) == B_OK) {
      BRect frame = parentWin->Frame();
      parentWin->Unlock();
      win->CenterIn(frame);
    } else {
      win->CenterOnScreen();
    }
    win->Unlock();
  }

  mRunning = true;
  mResult = nsIFilePicker::returnCancel;
  mPanel->Show();
  return NS_OK;
}

void
nsFilePicker::OnPanelClosed(int16_t aResult, const nsTArray<nsCString>& aPaths)
{
  MOZ_ASSERT(NS_IsMainThread());

  mFiles.Clear();
  for (uint32_t i = 0; i < aPaths.Length(); ++i) {
    nsCOMPtr<nsIFile> file;
    if (NS_SUCCEEDED(NS_NewNativeLocalFile(aPaths[i], true,
                                           getter_AddRefs(file)))) {
      mFiles.AppendObject(file);
    }
  }
  if (mMode == modeGetFolder && mFiles.Count() > 0) {
    mDisplayDirectory = mFiles[0];
  }

  mResult = aResult;
  mRunning = false;

  if (mPanel) {
    mPanel->Hide();
  }

  if (mCallback) {
    nsCOMPtr<nsIFilePickerShownCallback> callback = mCallback.forget();
    callback->Done(aResult);
  }
}

NS_IMETHODIMP
nsFilePicker::Open(nsIFilePickerShownCallback* aCallback)
{
  if (mRunning) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  mCallback = aCallback;
  nsresult rv = ShowPanel();
  if (NS_FAILED(rv)) {
    mCallback = nullptr;
    if (aCallback) {
      aCallback->Done(nsIFilePicker::returnCancel);
    }
    return NS_OK;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::Show(int16_t* aReturn)
{
  NS_ENSURE_ARG_POINTER(aReturn);
  if (mRunning) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsresult rv = ShowPanel();
  if (NS_FAILED(rv)) {
    *aReturn = nsIFilePicker::returnCancel;
    return rv;
  }
  nsIThread* thread = NS_GetCurrentThread();
  while (mRunning) {
    NS_ProcessNextEvent(thread, true);
  }
  *aReturn = mResult;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFile(nsIFile** aFile)
{
  NS_ENSURE_ARG_POINTER(aFile);
  *aFile = nullptr;
  if (mFiles.Count() > 0) {
    NS_ADDREF(*aFile = mFiles[0]);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFileURL(nsIURI** aFileURL)
{
  NS_ENSURE_ARG_POINTER(aFileURL);
  *aFileURL = nullptr;
  if (mFiles.Count() == 0) {
    return NS_OK;
  }
  return NS_NewFileURI(aFileURL, mFiles[0]);
}

NS_IMETHODIMP
nsFilePicker::GetFiles(nsISimpleEnumerator** aFiles)
{
  NS_ENSURE_ARG_POINTER(aFiles);
  return NS_NewArrayEnumerator(aFiles, mFiles);
}

NS_IMETHODIMP
nsFilePicker::AppendFilter(const nsAString& aTitle, const nsAString& aFilter)
{
  // TODO: hook into BRefFilter. Filters are recorded but not applied yet.
  mFilterTitles.AppendElement(aTitle);
  mFilters.AppendElement(aFilter);
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetDefaultString(nsAString& aString)
{
  aString = mDefaultFilename;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::SetDefaultString(const nsAString& aString)
{
  mDefaultFilename = aString;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetDefaultExtension(nsAString& aExtension)
{
  aExtension = mDefaultExtension;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::SetDefaultExtension(const nsAString& aExtension)
{
  mDefaultExtension = aExtension;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::GetFilterIndex(int32_t* aFilterIndex)
{
  NS_ENSURE_ARG_POINTER(aFilterIndex);
  *aFilterIndex = mSelectedTypeIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsFilePicker::SetFilterIndex(int32_t aFilterIndex)
{
  mSelectedTypeIndex = aFilterIndex;
  return NS_OK;
}
