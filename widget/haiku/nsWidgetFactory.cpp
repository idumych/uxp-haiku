/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ModuleUtils.h"
#include "mozilla/WidgetUtils.h"
#include "nsWidgetsCID.h"

#include "nsAppShell.h"
#include "nsAppShellSingleton.h"
#include "nsBidiKeyboard.h"
#include "nsClipboard.h"
#include "nsClipboardHelper.h"
#include "nsDragService.h"
#include "nsFilePicker.h"
#include "nsHTMLFormatConverter.h"
#include "nsIdleServiceHaiku.h"
#include "nsLookAndFeel.h"
#include "nsNativeThemeHaiku.h"
#include "nsScreenHaiku.h"
#include "nsSound.h"
#include "nsTransferable.h"
#include "nsWindow.h"
#include "GfxInfo.h"
#include "HaikuApplication.h"
#ifdef NS_PRINTING
#include "nsDeviceContextSpecHaiku.h"
#include "nsPrintOptionsImpl.h"
#include "nsPrintSession.h"
#endif

using namespace mozilla;
using namespace mozilla::widget;

NS_GENERIC_FACTORY_CONSTRUCTOR(nsWindow)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsTransferable)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsHTMLFormatConverter)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsBidiKeyboard)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsClipboard)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsClipboardHelper)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDragService)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsFilePicker)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsSound)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsScreenManagerHaiku)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsNativeThemeHaiku)
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(nsIdleServiceHaiku,
                                         nsIdleServiceHaiku::GetInstance)

#ifdef NS_PRINTING
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDeviceContextSpecHaiku)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsPrinterEnumeratorHaiku)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsPrintOptions, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsPrintSession, Init)
#endif

namespace mozilla {
namespace widget {
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(GfxInfo, Init)
}  // namespace widget
}  // namespace mozilla

NS_DEFINE_NAMED_CID(NS_WINDOW_CID);
NS_DEFINE_NAMED_CID(NS_CHILD_CID);
NS_DEFINE_NAMED_CID(NS_APPSHELL_CID);
NS_DEFINE_NAMED_CID(NS_FILEPICKER_CID);
NS_DEFINE_NAMED_CID(NS_SOUND_CID);
NS_DEFINE_NAMED_CID(NS_TRANSFERABLE_CID);
NS_DEFINE_NAMED_CID(NS_CLIPBOARD_CID);
NS_DEFINE_NAMED_CID(NS_CLIPBOARDHELPER_CID);
NS_DEFINE_NAMED_CID(NS_DRAGSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_HTMLFORMATCONVERTER_CID);
NS_DEFINE_NAMED_CID(NS_BIDIKEYBOARD_CID);
NS_DEFINE_NAMED_CID(NS_SCREENMANAGER_CID);
NS_DEFINE_NAMED_CID(NS_IDLE_SERVICE_CID);
NS_DEFINE_NAMED_CID(NS_GFXINFO_CID);
#ifdef NS_PRINTING
NS_DEFINE_NAMED_CID(NS_DEVICE_CONTEXT_SPEC_CID);
NS_DEFINE_NAMED_CID(NS_PRINTER_ENUMERATOR_CID);
NS_DEFINE_NAMED_CID(NS_PRINTSETTINGSSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_PRINTSESSION_CID);
#endif
NS_DEFINE_NAMED_CID(NS_THEMERENDERER_CID);

static const mozilla::Module::CIDEntry kWidgetCIDs[] = {
    {&kNS_WINDOW_CID, false, nullptr, nsWindowConstructor},
    {&kNS_CHILD_CID, false, nullptr, nsWindowConstructor},
    {&kNS_APPSHELL_CID, false, nullptr, nsAppShellConstructor,
     Module::ALLOW_IN_GPU_PROCESS},
    {&kNS_FILEPICKER_CID, false, nullptr, nsFilePickerConstructor,
     Module::MAIN_PROCESS_ONLY},
    {&kNS_SOUND_CID, false, nullptr, nsSoundConstructor,
     Module::MAIN_PROCESS_ONLY},
    {&kNS_TRANSFERABLE_CID, false, nullptr, nsTransferableConstructor},
    {&kNS_CLIPBOARD_CID, false, nullptr, nsClipboardConstructor,
     Module::MAIN_PROCESS_ONLY},
    {&kNS_CLIPBOARDHELPER_CID, false, nullptr, nsClipboardHelperConstructor},
    {&kNS_DRAGSERVICE_CID, false, nullptr, nsDragServiceConstructor,
     Module::MAIN_PROCESS_ONLY},
    {&kNS_HTMLFORMATCONVERTER_CID, false, nullptr,
     nsHTMLFormatConverterConstructor},
    {&kNS_BIDIKEYBOARD_CID, false, nullptr, nsBidiKeyboardConstructor,
     Module::MAIN_PROCESS_ONLY},
    {&kNS_SCREENMANAGER_CID, false, nullptr, nsScreenManagerHaikuConstructor,
     Module::MAIN_PROCESS_ONLY},
    {&kNS_IDLE_SERVICE_CID, false, nullptr, nsIdleServiceHaikuConstructor},
    {&kNS_GFXINFO_CID, false, nullptr, mozilla::widget::GfxInfoConstructor},
#ifdef NS_PRINTING
    {&kNS_DEVICE_CONTEXT_SPEC_CID, false, nullptr,
     nsDeviceContextSpecHaikuConstructor},
    {&kNS_PRINTER_ENUMERATOR_CID, false, nullptr,
     nsPrinterEnumeratorHaikuConstructor},
    {&kNS_PRINTSETTINGSSERVICE_CID, false, nullptr, nsPrintOptionsConstructor},
    {&kNS_PRINTSESSION_CID, false, nullptr, nsPrintSessionConstructor},
#endif
    {&kNS_THEMERENDERER_CID, false, nullptr, nsNativeThemeHaikuConstructor},
    {nullptr}};

static const mozilla::Module::ContractIDEntry kWidgetContracts[] = {
    {"@mozilla.org/widget/window/haiku;1", &kNS_WINDOW_CID},
    {"@mozilla.org/widgets/child_window/haiku;1", &kNS_CHILD_CID},
    {"@mozilla.org/widget/appshell/haiku;1", &kNS_APPSHELL_CID,
     Module::ALLOW_IN_GPU_PROCESS},
    {"@mozilla.org/filepicker;1", &kNS_FILEPICKER_CID,
     Module::MAIN_PROCESS_ONLY},
    {"@mozilla.org/sound;1", &kNS_SOUND_CID, Module::MAIN_PROCESS_ONLY},
    {"@mozilla.org/widget/transferable;1", &kNS_TRANSFERABLE_CID},
    {"@mozilla.org/widget/clipboard;1", &kNS_CLIPBOARD_CID,
     Module::MAIN_PROCESS_ONLY},
    {"@mozilla.org/widget/clipboardhelper;1", &kNS_CLIPBOARDHELPER_CID},
    {"@mozilla.org/widget/dragservice;1", &kNS_DRAGSERVICE_CID,
     Module::MAIN_PROCESS_ONLY},
    {"@mozilla.org/widget/htmlformatconverter;1",
     &kNS_HTMLFORMATCONVERTER_CID},
    {"@mozilla.org/widget/bidikeyboard;1", &kNS_BIDIKEYBOARD_CID,
     Module::MAIN_PROCESS_ONLY},
    {"@mozilla.org/gfx/screenmanager;1", &kNS_SCREENMANAGER_CID,
     Module::MAIN_PROCESS_ONLY},
    {"@mozilla.org/widget/idleservice;1", &kNS_IDLE_SERVICE_CID},
    {"@mozilla.org/gfx/info;1", &kNS_GFXINFO_CID},
#ifdef NS_PRINTING
    {"@mozilla.org/gfx/devicecontextspec;1", &kNS_DEVICE_CONTEXT_SPEC_CID},
    {"@mozilla.org/gfx/printerenumerator;1", &kNS_PRINTER_ENUMERATOR_CID},
    {"@mozilla.org/gfx/printsettings-service;1",
     &kNS_PRINTSETTINGSSERVICE_CID},
    {"@mozilla.org/gfx/printsession;1", &kNS_PRINTSESSION_CID},
#endif
    {"@mozilla.org/chrome/chrome-native-theme;1", &kNS_THEMERENDERER_CID},
    {nullptr}};

static void nsWidgetHaikuModuleDtor() {
  WidgetUtils::Shutdown();
  nsLookAndFeel::Shutdown();
  nsNativeThemeHaiku::Shutdown();
  nsAppShellShutdown();
  HaikuApplication::Shutdown();
}

static const mozilla::Module kWidgetModule = {
    mozilla::Module::kVersion,
    kWidgetCIDs,
    kWidgetContracts,
    nullptr,
    nullptr,
    nsAppShellInit,
    nsWidgetHaikuModuleDtor,
    Module::ALLOW_IN_GPU_PROCESS};

NSMODULE_DEFN(nsWidgetHaikuModule) = &kWidgetModule;
