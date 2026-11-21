/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Haiku keeps the user's language in its locale settings; GUI applications
// usually get no LANG/LC_* environment. Kept in its own file so Haiku's
// locale headers don't meet the in-tree ICU headers.

#include "nsString.h"

#pragma GCC visibility push(default)
#include <LocaleRoster.h>
#include <Message.h>
#pragma GCC visibility pop

bool
GetHaikuPreferredLanguage(nsACString& aLanguage)
{
  BLocaleRoster* roster = BLocaleRoster::Default();
  BMessage languages;
  const char* language = nullptr;
  if (!roster || roster->GetPreferredLanguages(&languages) != B_OK ||
      languages.FindString("language", &language) != B_OK ||
      !language || !*language) {
    return false;
  }
  aLanguage.Assign(language);
  return true;
}
