/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HaikuKeyboard.h"
#include "nsWindow.h"

#include <cctype>
#include <interface/InterfaceDefs.h>

#include "mozilla/BasicEvents.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/WidgetUtils.h"
#include "nsThreadUtils.h"
#include "nsString.h"

namespace mozilla::widget {

NS_IMPL_ISUPPORTS(HaikuTextInputListener, TextEventDispatcherListener,
                  nsISupportsWeakReference)

NS_IMETHODIMP
HaikuTextInputListener::NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                                  const IMENotification& aNotification) {
  return NS_OK;
}

mozilla::Modifiers GetGeckoModifiers(int32_t aHaikuModifiers) {
  mozilla::Modifiers mods = mozilla::MODIFIER_NONE;

  if (aHaikuModifiers & (B_SHIFT_KEY | B_LEFT_SHIFT_KEY | B_RIGHT_SHIFT_KEY)) {
    mods |= mozilla::MODIFIER_SHIFT;
  }

  if (aHaikuModifiers & (B_CONTROL_KEY | B_LEFT_CONTROL_KEY | B_RIGHT_CONTROL_KEY)) {
    mods |= mozilla::MODIFIER_ALT;
  }

  if (aHaikuModifiers & (B_COMMAND_KEY | B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY)) {
    mods |= mozilla::MODIFIER_CONTROL;
  }

  if (aHaikuModifiers & (B_OPTION_KEY | B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY)) {
    mods |= mozilla::MODIFIER_ALTGRAPH;
  }

  if (aHaikuModifiers & B_CAPS_LOCK) {
    mods |= mozilla::MODIFIER_CAPSLOCK;
  }
  if (aHaikuModifiers & B_NUM_LOCK) {
    mods |= mozilla::MODIFIER_NUMLOCK;
  }
  if (aHaikuModifiers & B_SCROLL_LOCK) {
    mods |= mozilla::MODIFIER_SCROLLLOCK;
  }

  return mods;
}

struct KeyMapEntry {
  int32_t mKey;
  uint32_t mKeyCode;
  KeyNameIndex mKeyNameIndex;
  CodeNameIndex mCodeNameIndex;
};

static const KeyMapEntry sKeyMap[] = {
    {0x01, NS_VK_ESCAPE, KEY_NAME_INDEX_Escape, CODE_NAME_INDEX_Escape},
    {0x02, NS_VK_F1, KEY_NAME_INDEX_F1, CODE_NAME_INDEX_F1},
    {0x03, NS_VK_F2, KEY_NAME_INDEX_F2, CODE_NAME_INDEX_F2},
    {0x04, NS_VK_F3, KEY_NAME_INDEX_F3, CODE_NAME_INDEX_F3},
    {0x05, NS_VK_F4, KEY_NAME_INDEX_F4, CODE_NAME_INDEX_F4},
    {0x06, NS_VK_F5, KEY_NAME_INDEX_F5, CODE_NAME_INDEX_F5},
    {0x07, NS_VK_F6, KEY_NAME_INDEX_F6, CODE_NAME_INDEX_F6},
    {0x08, NS_VK_F7, KEY_NAME_INDEX_F7, CODE_NAME_INDEX_F7},
    {0x09, NS_VK_F8, KEY_NAME_INDEX_F8, CODE_NAME_INDEX_F8},
    {0x0a, NS_VK_F9, KEY_NAME_INDEX_F9, CODE_NAME_INDEX_F9},
    {0x0b, NS_VK_F10, KEY_NAME_INDEX_F10, CODE_NAME_INDEX_F10},
    {0x0c, NS_VK_F11, KEY_NAME_INDEX_F11, CODE_NAME_INDEX_F11},
    {0x0d, NS_VK_F12, KEY_NAME_INDEX_F12, CODE_NAME_INDEX_F12},
    {0x0e, NS_VK_PRINTSCREEN, KEY_NAME_INDEX_PrintScreen, CODE_NAME_INDEX_PrintScreen},
    {0x0f, NS_VK_SCROLL_LOCK, KEY_NAME_INDEX_ScrollLock, CODE_NAME_INDEX_ScrollLock},
    {0x10, NS_VK_PAUSE, KEY_NAME_INDEX_Pause, CODE_NAME_INDEX_Pause},
    {0x11, NS_VK_BACK_QUOTE, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Backquote},
    {0x12, NS_VK_1, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit1},
    {0x13, NS_VK_2, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit2},
    {0x14, NS_VK_3, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit3},
    {0x15, NS_VK_4, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit4},
    {0x16, NS_VK_5, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit5},
    {0x17, NS_VK_6, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit6},
    {0x18, NS_VK_7, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit7},
    {0x19, NS_VK_8, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit8},
    {0x1a, NS_VK_9, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit9},
    {0x1b, NS_VK_0, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Digit0},
    {0x1c, NS_VK_HYPHEN_MINUS, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Minus},
    {0x1d, NS_VK_EQUALS, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Equal},
    {0x1e, NS_VK_BACK, KEY_NAME_INDEX_Backspace, CODE_NAME_INDEX_Backspace},
    {0x1f, NS_VK_INSERT, KEY_NAME_INDEX_Insert, CODE_NAME_INDEX_Insert},
    {0x20, NS_VK_HOME, KEY_NAME_INDEX_Home, CODE_NAME_INDEX_Home},
    {0x21, NS_VK_PAGE_UP, KEY_NAME_INDEX_PageUp, CODE_NAME_INDEX_PageUp},
    {0x34, NS_VK_DELETE, KEY_NAME_INDEX_Delete, CODE_NAME_INDEX_Delete},
    {0x35, NS_VK_END, KEY_NAME_INDEX_End, CODE_NAME_INDEX_End},
    {0x36, NS_VK_PAGE_DOWN, KEY_NAME_INDEX_PageDown, CODE_NAME_INDEX_PageDown},
    {0x57, NS_VK_UP, KEY_NAME_INDEX_ArrowUp, CODE_NAME_INDEX_ArrowUp},
    {0x61, NS_VK_LEFT, KEY_NAME_INDEX_ArrowLeft, CODE_NAME_INDEX_ArrowLeft},
    {0x62, NS_VK_DOWN, KEY_NAME_INDEX_ArrowDown, CODE_NAME_INDEX_ArrowDown},
    {0x63, NS_VK_RIGHT, KEY_NAME_INDEX_ArrowRight, CODE_NAME_INDEX_ArrowRight},
    {0x26, NS_VK_TAB, KEY_NAME_INDEX_Tab, CODE_NAME_INDEX_Tab},
    {0x27, NS_VK_Q, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyQ},
    {0x28, NS_VK_W, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyW},
    {0x29, NS_VK_E, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyE},
    {0x2a, NS_VK_R, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyR},
    {0x2b, NS_VK_T, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyT},
    {0x2c, NS_VK_Y, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyY},
    {0x2d, NS_VK_U, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyU},
    {0x2e, NS_VK_I, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyI},
    {0x2f, NS_VK_O, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyO},
    {0x30, NS_VK_P, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyP},
    {0x31, NS_VK_OPEN_BRACKET, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_BracketLeft},
    {0x32, NS_VK_CLOSE_BRACKET, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_BracketRight},
    {0x33, NS_VK_BACK_SLASH, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Backslash},
    {0x3b, NS_VK_CAPS_LOCK, KEY_NAME_INDEX_CapsLock, CODE_NAME_INDEX_CapsLock},
    {0x3c, NS_VK_A, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyA},
    {0x3d, NS_VK_S, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyS},
    {0x3e, NS_VK_D, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyD},
    {0x3f, NS_VK_F, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyF},
    {0x40, NS_VK_G, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyG},
    {0x41, NS_VK_H, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyH},
    {0x42, NS_VK_J, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyJ},
    {0x43, NS_VK_K, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyK},
    {0x44, NS_VK_L, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyL},
    {0x45, NS_VK_SEMICOLON, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Semicolon},
    {0x46, NS_VK_QUOTE, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Quote},
    {0x47, NS_VK_RETURN, KEY_NAME_INDEX_Enter, CODE_NAME_INDEX_Enter},
    {0x4b, NS_VK_SHIFT, KEY_NAME_INDEX_Shift, CODE_NAME_INDEX_ShiftLeft},
    {0x4c, NS_VK_Z, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyZ},
    {0x4d, NS_VK_X, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyX},
    {0x4e, NS_VK_C, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyC},
    {0x4f, NS_VK_V, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyV},
    {0x50, NS_VK_B, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyB},
    {0x51, NS_VK_N, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyN},
    {0x52, NS_VK_M, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_KeyM},
    {0x53, NS_VK_COMMA, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Comma},
    {0x54, NS_VK_PERIOD, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Period},
    {0x55, NS_VK_SLASH, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Slash},
    {0x56, NS_VK_SHIFT, KEY_NAME_INDEX_Shift, CODE_NAME_INDEX_ShiftRight},
    {0x5c, NS_VK_CONTROL, KEY_NAME_INDEX_Control, CODE_NAME_INDEX_ControlLeft},
    {0x5d, NS_VK_ALT, KEY_NAME_INDEX_Alt, CODE_NAME_INDEX_AltLeft},
    {0x5e, NS_VK_SPACE, KEY_NAME_INDEX_USE_STRING, CODE_NAME_INDEX_Space},
    {0x5f, NS_VK_ALT, KEY_NAME_INDEX_Alt, CODE_NAME_INDEX_AltRight},
    {0x60, NS_VK_CONTROL, KEY_NAME_INDEX_Control, CODE_NAME_INDEX_ControlRight},
    {0x66, NS_VK_META, KEY_NAME_INDEX_Meta, CODE_NAME_INDEX_MetaLeft},
    {0x67, NS_VK_META, KEY_NAME_INDEX_Meta, CODE_NAME_INDEX_MetaRight},
    {0x68, NS_VK_CONTEXT_MENU, KEY_NAME_INDEX_ContextMenu, CODE_NAME_INDEX_ContextMenu},
    {0x22, NS_VK_NUM_LOCK, KEY_NAME_INDEX_NumLock, CODE_NAME_INDEX_NumLock},
    {0x23, NS_VK_DIVIDE, KEY_NAME_INDEX_USE_STRING, CODE_NAME_INDEX_NumpadDivide},
    {0x24, NS_VK_MULTIPLY, KEY_NAME_INDEX_USE_STRING, CODE_NAME_INDEX_NumpadMultiply},
    {0x25, NS_VK_SUBTRACT, KEY_NAME_INDEX_USE_STRING, CODE_NAME_INDEX_NumpadSubtract},
    {0x37, NS_VK_NUMPAD7, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad7},
    {0x38, NS_VK_NUMPAD8, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad8},
    {0x39, NS_VK_NUMPAD9, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad9},
    {0x3a, NS_VK_ADD, KEY_NAME_INDEX_USE_STRING, CODE_NAME_INDEX_NumpadAdd},
    {0x48, NS_VK_NUMPAD4, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad4},
    {0x49, NS_VK_NUMPAD5, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad5},
    {0x4a, NS_VK_NUMPAD6, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad6},
    {0x58, NS_VK_NUMPAD1, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad1},
    {0x59, NS_VK_NUMPAD2, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad2},
    {0x5a, NS_VK_NUMPAD3, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad3},
    {0x5b, NS_VK_RETURN, KEY_NAME_INDEX_Enter, CODE_NAME_INDEX_NumpadEnter},
    {0x64, NS_VK_NUMPAD0, KEY_NAME_INDEX_Unidentified, CODE_NAME_INDEX_Numpad0},
    {0x65, NS_VK_DECIMAL, KEY_NAME_INDEX_USE_STRING, CODE_NAME_INDEX_NumpadDecimal},
};

static void MapHaikuKeyToGecko(int32_t aKey, uint32_t& aKeyCode,
                               KeyNameIndex& aKeyNameIndex,
                               CodeNameIndex& aCodeNameIndex) {
  aKeyCode = 0;
  aKeyNameIndex = KEY_NAME_INDEX_Unidentified;
  aCodeNameIndex = CODE_NAME_INDEX_UNKNOWN;

  for (const auto& entry : sKeyMap) {
    if (entry.mKey == aKey) {
      aKeyCode = entry.mKeyCode;
      aKeyNameIndex = entry.mKeyNameIndex;
      aCodeNameIndex = entry.mCodeNameIndex;
      return;
    }
  }
}

void HaikuKeyboard::DispatchKeyEvent(nsWindow* aWindow, bool aIsDown,
                                     int32_t aKey, int32_t aRawChar,
                                     int32_t aModifiers, const char* aBytes,
                                     int32_t aNumBytes, bool aIsRepeat) {
  if (!aWindow) {
    return;
  }

  RefPtr<nsWindow> window(aWindow);
  nsCString bytes(aBytes, aNumBytes);

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      [window, aIsDown, aKey, aRawChar, aModifiers, bytes, aIsRepeat]() {
        if (window->Destroyed() || !window->GetWidgetListener()) {
          return;
        }

        uint32_t keyCode = 0;
        KeyNameIndex keyNameIndex = KEY_NAME_INDEX_Unidentified;
        CodeNameIndex codeNameIndex = CODE_NAME_INDEX_UNKNOWN;
        MapHaikuKeyToGecko(aKey, keyCode, keyNameIndex, codeNameIndex);

        Modifiers geckoModifiers = GetGeckoModifiers(aModifiers);

        nsAutoString keyString;
        if (!bytes.IsEmpty()) {
          CopyUTF8toUTF16(bytes, keyString);
        }
        char16_t charCode = keyString.IsEmpty() ? 0 : keyString[0];

        // With Ctrl/Command held Haiku reports control characters; use the
        // unmodified character instead.
        if (charCode > 0 && charCode < 0x20 && charCode != 0x09 &&
            charCode != 0x0a && charCode != 0x0d && charCode != 0x08 &&
            charCode != 0x1b) {
          if (aRawChar >= 0x20 && aRawChar <= 0x7E) {
            char16_t c = static_cast<char16_t>(aRawChar);
            if (geckoModifiers & MODIFIER_SHIFT) {
              c = std::toupper(c);
            }
            keyString.Assign(c);
            charCode = c;
          } else {
            keyString.Truncate();
            charCode = 0;
          }
        }

        if (charCode == 0x0a || charCode == 0x0d || keyCode == NS_VK_RETURN) {
          keyCode = NS_VK_RETURN;
          keyNameIndex = KEY_NAME_INDEX_Enter;
          keyString.Truncate();
        } else if (charCode == 0x08 || keyCode == NS_VK_BACK) {
          keyCode = NS_VK_BACK;
          keyNameIndex = KEY_NAME_INDEX_Backspace;
          keyString.Truncate();
        } else if (charCode == 0x1b || keyCode == NS_VK_ESCAPE) {
          keyCode = NS_VK_ESCAPE;
          keyNameIndex = KEY_NAME_INDEX_Escape;
          keyString.Truncate();
        } else if (charCode == 0x09 || keyCode == NS_VK_TAB) {
          keyCode = NS_VK_TAB;
          keyNameIndex = KEY_NAME_INDEX_Tab;
          keyString.Truncate();
        }

        if ((keyNameIndex == KEY_NAME_INDEX_Unidentified ||
             keyNameIndex == KEY_NAME_INDEX_USE_STRING) &&
            !keyString.IsEmpty() && charCode >= 0x20) {
          keyNameIndex = KEY_NAME_INDEX_USE_STRING;
          if (!keyCode) {
            keyCode = WidgetUtils::ComputeKeyCodeFromChar(charCode);
          }
        }

        RefPtr<TextEventDispatcher> dispatcher =
            window->GetTextEventDispatcher();
        if (!dispatcher || NS_FAILED(dispatcher->BeginNativeInputTransaction())) {
          return;
        }

        WidgetKeyboardEvent event(true, aIsDown ? eKeyDown : eKeyUp, window);
        event.mKeyCode = keyCode;
        event.mKeyNameIndex = keyNameIndex;
        event.mCodeNameIndex = codeNameIndex;
        event.mModifiers = geckoModifiers;
        event.mIsRepeat = aIsDown && aIsRepeat;
        if (keyNameIndex == KEY_NAME_INDEX_USE_STRING) {
          event.mKeyValue = keyString;
        }

        nsEventStatus status = nsEventStatus_eIgnore;
        dispatcher->DispatchKeyboardEvent(event.mMessage, event, status);
        if (aIsDown && !window->Destroyed() &&
            status != nsEventStatus_eConsumeNoDefault) {
          dispatcher->MaybeDispatchKeypressEvents(event, status);
        }
      }));
}

}  // namespace mozilla::widget
