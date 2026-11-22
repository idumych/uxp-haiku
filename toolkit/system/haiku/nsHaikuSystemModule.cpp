/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsToolkitCompsCID.h"
#include "mozilla/ModuleUtils.h"

#include "nsHaikuAlertsService.h"

NS_GENERIC_FACTORY_CONSTRUCTOR(nsHaikuAlertsService)

NS_DEFINE_NAMED_CID(NS_SYSTEMALERTSSERVICE_CID);

static const mozilla::Module::CIDEntry kHaikuSystemCIDs[] = {
  { &kNS_SYSTEMALERTSSERVICE_CID, false, nullptr, nsHaikuAlertsServiceConstructor },
  { nullptr }
};

static const mozilla::Module::ContractIDEntry kHaikuSystemContracts[] = {
  { NS_SYSTEMALERTSERVICE_CONTRACTID, &kNS_SYSTEMALERTSSERVICE_CID },
  { nullptr }
};

// Runs before the browser's handlers ("m-browser") so a notification click
// is consumed before a window would be opened.
static const mozilla::Module::CategoryEntry kHaikuSystemCategories[] = {
  { "command-line-handler", "a-haiku-alerts", NS_SYSTEMALERTSERVICE_CONTRACTID },
  { nullptr }
};

static const mozilla::Module kHaikuSystemModule = {
  mozilla::Module::kVersion,
  kHaikuSystemCIDs,
  kHaikuSystemContracts,
  kHaikuSystemCategories
};

NSMODULE_DEFN(mozhaikusystem) = &kHaikuSystemModule;
