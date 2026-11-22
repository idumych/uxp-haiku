/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsFilePicker_h__
#define nsFilePicker_h__

#include "nsBaseFilePicker.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsTArray.h"

// Haiku API classes: declare them with default visibility, or gcc_hidden.h makes
// them hidden and libxul can't link against libbe.
#pragma GCC visibility push(default)
class BFilePanel;
class BRefFilter;
#pragma GCC visibility pop
class FilePanelLooper;
class nsIWidget;

class nsFilePicker final : public nsBaseFilePicker
{
public:
  nsFilePicker();

  NS_DECL_ISUPPORTS

  // nsIFilePicker (less what's in nsBaseFilePicker)
  NS_IMETHOD Open(nsIFilePickerShownCallback* aCallback) override;
  NS_IMETHOD Show(int16_t* aReturn) override;
  NS_IMETHOD AppendFilter(const nsAString& aTitle,
                          const nsAString& aFilter) override;
  NS_IMETHOD GetDefaultString(nsAString& aString) override;
  NS_IMETHOD SetDefaultString(const nsAString& aString) override;
  NS_IMETHOD GetDefaultExtension(nsAString& aExtension) override;
  NS_IMETHOD SetDefaultExtension(const nsAString& aExtension) override;
  NS_IMETHOD GetFilterIndex(int32_t* aFilterIndex) override;
  NS_IMETHOD SetFilterIndex(int32_t aFilterIndex) override;
  NS_IMETHOD GetFile(nsIFile** aFile) override;
  NS_IMETHOD GetFileURL(nsIURI** aFileURL) override;
  NS_IMETHOD GetFiles(nsISimpleEnumerator** aFiles) override;

  // Called on the main thread when the panel reports a result.
  void OnPanelClosed(int16_t aResult, const nsTArray<nsCString>& aPaths);

protected:
  virtual ~nsFilePicker();

  void InitNative(nsIWidget* aParent, const nsAString& aTitle) override;

private:
  nsresult ShowPanel();
  void ClosePanel();

  nsCOMPtr<nsIWidget> mParentWidget;
  nsCOMPtr<nsIFilePickerShownCallback> mCallback;
  nsCOMArray<nsIFile> mFiles;
  nsString mTitle;
  nsString mDefaultFilename;
  nsString mDefaultExtension;
  nsTArray<nsString> mFilters;
  nsTArray<nsString> mFilterTitles;
  int32_t mSelectedTypeIndex;
  int16_t mResult;
  bool mRunning;

  BFilePanel* mPanel;
  FilePanelLooper* mLooper;
  BRefFilter* mRefFilter;
};

#endif // nsFilePicker_h__
