/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsLookAndFeel.h"

#include "HaikuUtils.h"
#include "gfxFontConstants.h"
#include "nsStyleConsts.h"

#include <string.h>

#pragma GCC visibility push(default)
#include <interface/Menu.h>
#include <interface/ScrollBar.h>
#pragma GCC visibility pop

using mozilla::LookAndFeel;

static inline nscolor
ToNSColor(rgb_color c)
{
  return NS_RGB(c.red, c.green, c.blue);
}

static rgb_color
BlendColors(rgb_color a, rgb_color b)
{
  rgb_color r = a;
  r.red = uint8((int32(a.red) + int32(b.red) + 1) / 2);
  r.green = uint8((int32(a.green) + int32(b.green) + 1) / 2);
  r.blue = uint8((int32(a.blue) + int32(b.blue) + 1) / 2);
  return r;
}

nsLookAndFeel::nsLookAndFeel()
  : nsXPLookAndFeel()
  , mInitialized(false)
{
}

nsLookAndFeel::~nsLookAndFeel()
{
}

nsresult
nsLookAndFeel::NativeGetColor(ColorID aID, nscolor& aColor)
{
  EnsureInit();

  const rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
  const rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);
  const rgb_color docBg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
  const rgb_color docText = ui_color(B_DOCUMENT_TEXT_COLOR);
  const rgb_color controlBg = ui_color(B_CONTROL_BACKGROUND_COLOR);
  const rgb_color controlText = ui_color(B_CONTROL_TEXT_COLOR);
  const rgb_color selBg = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
  const rgb_color selText = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);

  nsresult res = NS_OK;

  switch (aID) {
    case eColorID_WindowBackground:
    case eColorID_TextBackground:
    case eColorID__moz_field:
    case eColorID__moz_eventreerow:
    case eColorID__moz_combobox:
      aColor = ToNSColor(docBg);
      break;
    case eColorID_WindowForeground:
    case eColorID_TextForeground:
    case eColorID__moz_fieldtext:
    case eColorID__moz_comboboxtext:
      aColor = ToNSColor(docText);
      break;
    case eColorID_WidgetBackground:
    case eColorID_activecaption:
    case eColorID_appworkspace:
    case eColorID_background:
    case eColorID_window:
    case eColorID_windowframe:
    case eColorID__moz_dialog:
    case eColorID_activeborder:
    case eColorID_inactiveborder:
    case eColorID_inactivecaption:
      aColor = ToNSColor(panelBg);
      break;
    case eColorID_WidgetForeground:
    case eColorID_captiontext:
    case eColorID_windowtext:
    case eColorID__moz_dialogtext:
    case eColorID__moz_gtk_info_bar_text:
      aColor = ToNSColor(panelText);
      break;
    case eColorID_WidgetSelectBackground:
    case eColorID_TextSelectBackground:
    case eColorID_IMESelectedRawTextBackground:
    case eColorID_IMESelectedConvertedTextBackground:
    case eColorID__moz_dragtargetzone:
    case eColorID__moz_cellhighlight:
    case eColorID__moz_html_cellhighlight:
    case eColorID_highlight:
      aColor = ToNSColor(selBg);
      break;
    case eColorID_WidgetSelectForeground:
    case eColorID_TextSelectForeground:
    case eColorID_IMESelectedRawTextForeground:
    case eColorID_IMESelectedConvertedTextForeground:
    case eColorID_highlighttext:
    case eColorID__moz_cellhighlighttext:
    case eColorID__moz_html_cellhighlighttext:
      aColor = ToNSColor(selText);
      break;
    case eColorID_Widget3DHighlight:
    case eColorID_buttonhighlight:
    case eColorID_threedhighlight:
      aColor = ToNSColor(ui_color(B_SHINE_COLOR));
      break;
    case eColorID_Widget3DShadow:
    case eColorID_buttonshadow:
    case eColorID_threedshadow:
      aColor = ToNSColor(tint_color(panelBg, B_DARKEN_2_TINT));
      break;
    case eColorID_threeddarkshadow:
      aColor = ToNSColor(ui_color(B_SHADOW_COLOR));
      break;
    case eColorID_threedlightshadow:
    case eColorID_threedface:
    case eColorID_buttonface:
      aColor = ToNSColor(controlBg);
      break;
    case eColorID_buttontext:
      aColor = ToNSColor(controlText);
      break;
    case eColorID__moz_buttondefault:
      aColor = ToNSColor(ui_color(B_CONTROL_BORDER_COLOR));
      break;
    case eColorID__moz_buttonhoverface:
      aColor = ToNSColor(tint_color(controlBg, B_DARKEN_1_TINT));
      break;
    case eColorID__moz_buttonhovertext:
      aColor = ToNSColor(controlText);
      break;
    case eColorID_graytext:
    case eColorID_inactivecaptiontext:
      aColor = ToNSColor(BlendColors(panelText, panelBg));
      break;
    case eColorID_IMERawInputBackground:
    case eColorID_IMEConvertedTextBackground:
      aColor = NS_TRANSPARENT;
      break;
    case eColorID_IMERawInputForeground:
    case eColorID_IMEConvertedTextForeground:
    case eColorID_IMERawInputUnderline:
    case eColorID_IMEConvertedTextUnderline:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case eColorID_IMESelectedRawTextUnderline:
    case eColorID_IMESelectedConvertedTextUnderline:
      aColor = NS_TRANSPARENT;
      break;
    case eColorID_SpellCheckerUnderline:
      aColor = NS_RGB(0xff, 0, 0);
      break;
    case eColorID_infobackground:
      aColor = ToNSColor(ui_color(B_TOOL_TIP_BACKGROUND_COLOR));
      break;
    case eColorID_infotext:
      aColor = ToNSColor(ui_color(B_TOOL_TIP_TEXT_COLOR));
      break;
    case eColorID_menu:
      aColor = ToNSColor(ui_color(B_MENU_BACKGROUND_COLOR));
      break;
    case eColorID_menutext:
    case eColorID__moz_menubartext:
      aColor = ToNSColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
      break;
    case eColorID__moz_menuhover:
      aColor = ToNSColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
      break;
    case eColorID__moz_menuhovertext:
    case eColorID__moz_menubarhovertext:
      aColor = ToNSColor(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR));
      break;
    case eColorID_scrollbar:
      aColor = ToNSColor(tint_color(panelBg, 1.08f));
      break;
    case eColorID__moz_oddtreerow:
      aColor = ToNSColor(tint_color(docBg, 1.04f));
      break;
    case eColorID__moz_nativehyperlinktext:
      aColor = ToNSColor(ui_color(B_LINK_TEXT_COLOR));
      break;
    default:
      aColor = 0;
      res = NS_ERROR_FAILURE;
      break;
  }

  return res;
}

nsresult
nsLookAndFeel::GetIntImpl(IntID aID, int32_t& aResult)
{
  switch (aID) {
    case eIntID_ScrollButtonLeftMouseButtonAction:
      aResult = 0;
      return NS_OK;
    case eIntID_ScrollButtonMiddleMouseButtonAction:
      aResult = 1;
      return NS_OK;
    case eIntID_ScrollButtonRightMouseButtonAction:
      aResult = 2;
      return NS_OK;
    default:
      break;
  }

  nsresult res = nsXPLookAndFeel::GetIntImpl(aID, aResult);
  if (NS_SUCCEEDED(res)) {
    return res;
  }
  res = NS_OK;

  switch (aID) {
    case eIntID_CaretBlinkTime:
      aResult = 500;
      break;
    case eIntID_CaretWidth:
      aResult = 1;
      break;
    case eIntID_ShowCaretDuringSelection:
      aResult = 0;
      break;
    case eIntID_SelectTextfieldsOnKeyFocus:
      aResult = 1;
      break;
    case eIntID_SubmenuDelay:
      aResult = 200;
      break;
    case eIntID_TooltipDelay:
      aResult = 500;
      break;
    case eIntID_MenusCanOverlapOSBar:
      aResult = 0;
      break;
    case eIntID_SkipNavigatingDisabledMenuItem:
      aResult = 1;
      break;
    case eIntID_DragThresholdX:
    case eIntID_DragThresholdY:
      aResult = 4;
      break;
    case eIntID_ScrollArrowStyle: {
      scroll_bar_info info;
      if (get_scroll_bar_info(&info) == B_OK && info.double_arrows) {
        aResult = eScrollArrowStyle_BothAtEachEnd;
      } else {
        aResult = eScrollArrowStyle_Single;
      }
      break;
    }
    case eIntID_ScrollSliderStyle:
      aResult = eScrollThumbStyle_Proportional;
      break;
    case eIntID_ScrollToClick:
      aResult = 0;
      break;
    case eIntID_TreeOpenDelay:
    case eIntID_TreeCloseDelay:
      aResult = 1000;
      break;
    case eIntID_TreeLazyScrollDelay:
      aResult = 150;
      break;
    case eIntID_TreeScrollDelay:
      aResult = 100;
      break;
    case eIntID_TreeScrollLinesMax:
      aResult = 3;
      break;
    case eIntID_DWMCompositor:
    case eIntID_WindowsClassic:
    case eIntID_WindowsDefaultTheme:
    case eIntID_WindowsThemeIdentifier:
    case eIntID_OperatingSystemVersionIdentifier:
    case eIntID_MacGraphiteTheme:
    case eIntID_MacLionTheme:
    case eIntID_TouchEnabled:
      aResult = 0;
      res = NS_ERROR_NOT_IMPLEMENTED;
      break;
    case eIntID_AlertNotificationOrigin:
      aResult = NS_ALERT_TOP;
      break;
    case eIntID_IMERawInputUnderlineStyle:
    case eIntID_IMEConvertedTextUnderlineStyle:
      aResult = NS_STYLE_TEXT_DECORATION_STYLE_SOLID;
      break;
    case eIntID_IMESelectedRawTextUnderlineStyle:
    case eIntID_IMESelectedConvertedTextUnderline:
      aResult = NS_STYLE_TEXT_DECORATION_STYLE_NONE;
      break;
    case eIntID_SpellCheckerUnderlineStyle:
      aResult = NS_STYLE_TEXT_DECORATION_STYLE_WAVY;
      break;
    case eIntID_MenuBarDrag:
      aResult = 0;
      break;
    case eIntID_ScrollbarButtonAutoRepeatBehavior:
      aResult = 1;
      break;
    case eIntID_SwipeAnimationEnabled:
      aResult = 0;
      break;
    case eIntID_ColorPickerAvailable:
      aResult = 0;
      break;
    case eIntID_ContextMenuOffsetVertical:
    case eIntID_ContextMenuOffsetHorizontal:
      aResult = 2;
      break;
    default:
      aResult = 0;
      res = NS_ERROR_FAILURE;
  }

  return res;
}

nsresult
nsLookAndFeel::GetFloatImpl(FloatID aID, float& aResult)
{
  nsresult res = nsXPLookAndFeel::GetFloatImpl(aID, aResult);
  if (NS_SUCCEEDED(res)) {
    return res;
  }
  res = NS_OK;

  switch (aID) {
    case eFloatID_IMEUnderlineRelativeSize:
    case eFloatID_SpellCheckerUnderlineRelativeSize:
      aResult = 1.0f;
      break;
    case eFloatID_CaretAspectRatio:
      aResult = 0.0f;
      break;
    default:
      aResult = -1.0f;
      res = NS_ERROR_FAILURE;
  }
  return res;
}

/* static */ void
nsLookAndFeel::InitSystemFont(const char* aFamily, float aSize,
                              uint16_t aFace, SystemFont& aFont)
{
  if (!aFamily || !aFamily[0]) {
    aFont.mName.AssignLiteral("sans-serif");
  } else {
    NS_ConvertUTF8toUTF16 family(aFamily);
    aFont.mName = NS_LITERAL_STRING("\"") + family + NS_LITERAL_STRING("\"");
  }

  // Haiku font sizes are in points at 72 dpi, i.e. effectively pixels.
  aFont.mStyle.size = aSize > 0.0f ? aSize : 12.0f;
  aFont.mStyle.systemFont = true;
  aFont.mStyle.style = (aFace & B_ITALIC_FACE) ? NS_FONT_STYLE_ITALIC
                                               : NS_FONT_STYLE_NORMAL;
  if (aFace & B_HEAVY_FACE) {
    aFont.mStyle.weight = 800;
  } else if (aFace & B_BOLD_FACE) {
    aFont.mStyle.weight = NS_FONT_WEIGHT_BOLD;
  } else if (aFace & B_LIGHT_FACE) {
    aFont.mStyle.weight = 300;
  } else {
    aFont.mStyle.weight = NS_FONT_WEIGHT_NORMAL;
  }
  aFont.mStyle.stretch = (aFace & B_CONDENSED_FACE) ? NS_FONT_STRETCH_CONDENSED
                                                    : NS_FONT_STRETCH_NORMAL;
}

void
nsLookAndFeel::EnsureInit()
{
  if (mInitialized) {
    return;
  }
  mozilla::widget::EnsureHaikuApplication();
  mInitialized = true;

  InitSystemFont(nullptr, 12.0f, 0, mPlainFont);
  if (be_plain_font) {
    font_family family;
    font_style style;
    be_plain_font->GetFamilyAndStyle(&family, &style);
    InitSystemFont(family, be_plain_font->Size(), be_plain_font->Face(),
                   mPlainFont);
  }

  menu_info menuInfo;
  if (get_menu_info(&menuInfo) == B_OK) {
    InitSystemFont(menuInfo.f_family, menuInfo.font_size, 0, mMenuFont);
  } else {
    mMenuFont.mName = mPlainFont.mName;
    mMenuFont.mStyle = mPlainFont.mStyle;
  }
}

bool
nsLookAndFeel::GetFontImpl(FontID aID, nsString& aFontName,
                           gfxFontStyle& aFontStyle,
                           float aDevPixPerCSSPixel)
{
  EnsureInit();

  const SystemFont& font =
    (aID == eFont_Menu || aID == eFont_PullDownMenu) ? mMenuFont : mPlainFont;
  aFontName = font.mName;
  aFontStyle = font.mStyle;
  aFontStyle.systemFont = true;
  return true;
}

void
nsLookAndFeel::RefreshImpl()
{
  nsXPLookAndFeel::RefreshImpl();
  mInitialized = false;
}
