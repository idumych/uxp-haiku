/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Minimal nsISound: system beep and the Haiku system sound events.
 * Playing arbitrary URLs is not supported yet. */

#include "nsSound.h"

#include "HaikuUtils.h"
#include "nsString.h"

#pragma GCC visibility push(default)
#include <support/Beep.h>
#pragma GCC visibility pop

NS_IMPL_ISUPPORTS(nsSound, nsISound)

nsSound::nsSound()
{
}

nsSound::~nsSound()
{
}

NS_IMETHODIMP
nsSound::Init()
{
  return NS_OK;
}

NS_IMETHODIMP
nsSound::Beep()
{
  beep();
  return NS_OK;
}

NS_IMETHODIMP
nsSound::Play(nsIURL* aURL)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsSound::PlaySystemSound(const nsAString& aSoundAlias)
{
  if (aSoundAlias.EqualsLiteral("_moz_mailbeep")) {
    return Beep();
  }
  // Other aliases are ignored, like on GTK without a sound theme.
  return NS_OK;
}

NS_IMETHODIMP
nsSound::PlayEventSound(uint32_t aEventId)
{
  switch (aEventId) {
    case EVENT_NEW_MAIL_RECEIVED:
    case EVENT_ALERT_DIALOG_OPEN:
    case EVENT_CONFIRM_DIALOG_OPEN:
      beep();
      break;
    default:
      break;
  }
  return NS_OK;
}
