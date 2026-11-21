/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Haiku keeps the system time zone in its locale settings rather than in
 * TZ or /etc/localtime; see JS::detail::InitWithFailureDiagnostic. Kept in its own
 * file so Haiku's locale headers (which refer to the system ICU namespace)
 * never meet the in-tree ICU headers.
 */

#include <string.h>

#pragma GCC visibility push(default)
#include <LocaleRoster.h>
#include <String.h>
#include <TimeZone.h>
#pragma GCC visibility pop

namespace js {

bool
GetHaikuDefaultTimeZoneID(char* buffer, size_t length)
{
    BLocaleRoster* roster = BLocaleRoster::Default();
    if (!roster || length == 0)
        return false;

    BTimeZone timeZone;
    if (roster->GetDefaultTimeZone(&timeZone) != B_OK)
        return false;

    const BString& id = timeZone.ID();
    if (id.Length() <= 0 || size_t(id.Length()) >= length)
        return false;

    strcpy(buffer, id.String());
    return true;
}

} // namespace js
