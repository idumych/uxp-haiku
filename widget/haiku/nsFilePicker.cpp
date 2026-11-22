/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* File picker on top of BFilePanel. The panel replies to a private BLooper,
 * which forwards the result to the main thread. Show() (the deprecated
 * synchronous API, also used by nsBaseFilePicker::Open's default runnable)
 * spins the Gecko event loop until the panel is closed. */

#include "nsFilePicker.h"

#include <fnmatch.h>
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif

#include "HaikuUtils.h"
#include "nsArrayEnumerator.h"
#include "nsIWidget.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"

using namespace mozilla;

// Shows directories, plus the files matching one nsIFilePicker filter
// ("*.html; *.htm"). Patterns are matched case-insensitively.
class PatternRefFilter : public BRefFilter
{
public:
  explicit PatternRefFilter(const nsAString& aFilter)
  {
    NS_ConvertUTF16toUTF8 filter(aFilter);
    int32_t start = 0;
    while (start < int32_t(filter.Length())) {
      int32_t end = filter.FindChar(';', start);
      if (end < 0) {
        end = filter.Length();
      }
      nsAutoCString pattern(Substring(filter, start, end - start));
      pattern.Trim(" ");
      if (!pattern.IsEmpty()) {
        mPatterns.AppendElement(pattern);
      }
      start = end + 1;
    }
  }

  bool Filter(const entry_ref* aRef, BNode* aNode, struct stat_beos* aStat,
              const char* aMimeType) override
  {
    if (aNode && aNode->IsDirectory()) {
      return true;
    }
    if (!aRef || !aRef->name) {
      return false;
    }
    // Follow symlinks to directories.
    BEntry entry(aRef, true);
    if (entry.IsDirectory()) {
      return true;
    }
    for (const nsCString& pattern : mPatterns) {
      if (pattern.EqualsLiteral("*") || pattern.EqualsLiteral("..apps") ||
          fnmatch(pattern.get(), aRef->name, FNM_CASEFOLD) == 0) {
        return true;
      }
    }
    return mPatterns.IsEmpty();
  }

private:
  nsTArray<nsCString> mPatterns;
};

class FilePanelLooper : public BLooper
{
public:
  explicit FilePanelLooper(nsFilePicker* aPicker)
    : BLooper("gecko_file_panel")
    , mPicker(new mozilla::widget::MainThreadWeakRef<nsFilePicker>(aPicker))
    , mDone(false)
  {
  }

  // Called on the main thread before the picker goes away.
  void DetachPicker() { mPicker->Clear(); }

  void MessageReceived(BMessage* aMessage) override
  {
    if (mDone) {
      return;
    }

    int16_t result = nsIFilePicker::returnCancel;
    nsTArray<nsCString> paths;

    switch (aMessage->what) {
      case B_REFS_RECEIVED: {
        entry_ref ref;
        for (int32 i = 0; aMessage->FindRef("refs", i, &ref) == B_OK; ++i) {
          BPath path(&ref);
          if (path.InitCheck() == B_OK) {
            paths.AppendElement(nsCString(path.Path()));
          }
        }
        result = paths.IsEmpty() ? nsIFilePicker::returnCancel
                                 : nsIFilePicker::returnOK;
        break;
      }
      case B_SAVE_REQUESTED: {
        entry_ref dirRef;
        const char* name = nullptr;
        if (aMessage->FindRef("directory", &dirRef) == B_OK &&
            aMessage->FindString("name", &name) == B_OK && name) {
          BDirectory dir(&dirRef);
          BPath path(&dir, name);
          if (path.InitCheck() == B_OK) {
            paths.AppendElement(nsCString(path.Path()));
            // BFilePanel already asked the user before overwriting.
            result = BEntry(path.Path()).Exists() ? nsIFilePicker::returnReplace
                                                  : nsIFilePicker::returnOK;
          }
        }
        break;
      }
      case B_CANCEL:
        break;
      default:
        BLooper::MessageReceived(aMessage);
        return;
    }

    mDone = true;
    // Don't AddRef the picker on this thread; resolve it on the main thread.
    RefPtr<mozilla::widget::MainThreadWeakRef<nsFilePicker>> ref = mPicker;
    NS_DispatchToMainThread(NS_NewRunnableFunction([ref, result, paths]() {
      RefPtr<nsFilePicker> picker = ref->get();
      if (picker) {
        picker->OnPanelClosed(result, paths);
      }
    }));
  }

private:
  RefPtr<mozilla::widget::MainThreadWeakRef<nsFilePicker>> mPicker;
  bool mDone;
};

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
    mLooper->DetachPicker();
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

  mLooper = new FilePanelLooper(this);
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
    mRefFilter = new PatternRefFilter(mFilters[index]);
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
