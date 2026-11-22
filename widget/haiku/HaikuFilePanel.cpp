/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* The BLooper and BRefFilter subclasses behind nsFilePicker. Built with -frtti:
 * libbe dynamic_casts handlers, so classes derived from Be classes need typeinfo.
 * Keep Gecko polymorphic types (runnables, XPCOM classes) out of this file: their
 * typeinfo doesn't exist, because the rest of Gecko is built without RTTI. */

#include "HaikuFilePanel.h"

#include <fnmatch.h>
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif

#include "HaikuUtils.h"
#include "nsFilePicker.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla {
namespace widget {

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
    , mPicker(new MainThreadWeakRef<nsFilePicker>(aPicker))
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
    DeliverFilePanelResult(mPicker, result, std::move(paths));
  }

private:
  RefPtr<MainThreadWeakRef<nsFilePicker>> mPicker;
  bool mDone;
};

BRefFilter* CreatePatternRefFilter(const nsAString& aFilter)
{
  return new PatternRefFilter(aFilter);
}

BLooper* CreateFilePanelLooper(nsFilePicker* aPicker)
{
  return new FilePanelLooper(aPicker);
}

void DetachFilePanelLooper(BLooper* aLooper)
{
  static_cast<FilePanelLooper*>(aLooper)->DetachPicker();
}

} // namespace widget
} // namespace mozilla
