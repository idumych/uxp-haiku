/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIconChannel.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#pragma GCC visibility push(default)
#include <Bitmap.h>
#include <Entry.h>
#include <Font.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <View.h>
#pragma GCC visibility pop

#include "mozilla/EndianUtils.h"
#include "nsComponentManagerUtils.h"
#include "nsIFile.h"
#include "nsIFileURL.h"
#include "nsIStringStream.h"
#include "nsIURL.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsNullPrincipal.h"

NS_IMPL_ISUPPORTS(nsIconChannel,
                  nsIRequest,
                  nsIChannel)

// Turn a B_RGBA32 bitmap (B, G, R, A in memory) into the image/icon format
// read by nsIconDecoder: width, height, then premultiplied pixels in the
// platform's native 32-bit order.
static nsresult
BitmapToChannel(BBitmap* aBitmap, nsIURI* aURI, nsIChannel** aChannel)
{
  const int width = aBitmap->Bounds().IntegerWidth() + 1;
  const int height = aBitmap->Bounds().IntegerHeight() + 1;
  NS_ENSURE_TRUE(width > 0 && width < 256 && height > 0 && height < 256,
                 NS_ERROR_UNEXPECTED);

  const size_t bufSize = 2 + 4 * width * height;
  uint8_t* const buf = (uint8_t*)moz_xmalloc(bufSize);
  uint8_t* out = buf;
  *(out++) = width;
  *(out++) = height;

  const uint8_t* bits = (const uint8_t*)aBitmap->Bits();
  const int32 bpr = aBitmap->BytesPerRow();
  for (int y = 0; y < height; ++y) {
    const uint8_t* in = bits + y * bpr;
    for (int x = 0; x < width; ++x) {
      uint8_t b = *(in++);
      uint8_t g = *(in++);
      uint8_t r = *(in++);
      uint8_t a = *(in++);
#define DO_PREMULTIPLY(c_) uint8_t(uint16_t(c_) * uint16_t(a) / uint16_t(255))
#if MOZ_LITTLE_ENDIAN
      *(out++) = DO_PREMULTIPLY(b);
      *(out++) = DO_PREMULTIPLY(g);
      *(out++) = DO_PREMULTIPLY(r);
      *(out++) = a;
#else
      *(out++) = a;
      *(out++) = DO_PREMULTIPLY(r);
      *(out++) = DO_PREMULTIPLY(g);
      *(out++) = DO_PREMULTIPLY(b);
#endif
#undef DO_PREMULTIPLY
    }
  }

  nsresult rv;
  nsCOMPtr<nsIStringInputStream> stream =
    do_CreateInstance("@mozilla.org/io/string-input-stream;1", &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    free(buf);
    return rv;
  }
  rv = stream->AdoptData((char*)buf, bufSize);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> nullPrincipal = nsNullPrincipal::Create();
  return NS_NewInputStreamChannel(aChannel,
                                  aURI,
                                  stream,
                                  nullPrincipal,
                                  nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED,
                                  nsIContentPolicy::TYPE_INTERNAL_IMAGE,
                                  NS_LITERAL_CSTRING(IMAGE_ICON_MS));
}

static bool
GetMimeIcon(const char* aType, BBitmap* aBitmap, icon_size aSize)
{
  BMimeType mime(aType);
  if (mime.InitCheck() != B_OK)
    return false;

  // Vector icon of the type itself, then of its preferred app, then the
  // supertype; finally any old-style bitmap icon.
  uint8* data = nullptr;
  size_t size = 0;
  if (mime.GetIcon(&data, &size) == B_OK) {
    bool ok = BIconUtils::GetVectorIcon(data, size, aBitmap) == B_OK;
    free(data);
    if (ok)
      return true;
  }
  char app[B_MIME_TYPE_LENGTH];
  if (mime.GetPreferredApp(app) == B_OK) {
    BMimeType appMime(app);
    if (appMime.GetIconForType(aType, &data, &size) == B_OK) {
      bool ok = BIconUtils::GetVectorIcon(data, size, aBitmap) == B_OK;
      free(data);
      if (ok)
        return true;
    }
  }
  BMimeType super;
  if (mime.GetSupertype(&super) == B_OK && super.GetIcon(&data, &size) == B_OK) {
    bool ok = BIconUtils::GetVectorIcon(data, size, aBitmap) == B_OK;
    free(data);
    if (ok)
      return true;
  }
  return mime.GetIcon(aBitmap, aSize) == B_OK;
}

// Copy a drawn B_RGBA32 bitmap into aDest (same size, maybe another stride).
static void
CopyBitmap(BBitmap* aSource, BBitmap* aDest)
{
  const int32 rows = aDest->Bounds().IntegerHeight() + 1;
  const int32 rowBytes = std::min(aSource->BytesPerRow(), aDest->BytesPerRow());
  for (int32 y = 0; y < rows; y++) {
    memcpy((uint8*)aDest->Bits() + y * aDest->BytesPerRow(),
           (const uint8*)aSource->Bits() + y * aSource->BytesPerRow(),
           rowBytes);
  }
}

// The GTK stock icons the Linux themes ask for (moz-icon://stock/gtk-...).
// Haiku has no stock icon set, so we use a Haiku icon where one fits and
// otherwise draw a simple glyph in the menu text colour.
enum class Glyph {
  None, ArrowLeft, ArrowRight, ArrowUp, ArrowDown, Cross, Plus, Minus,
  Refresh, Play, Pause, Check, Find, Undo, Redo, Warning, Error, Info,
  Question
};

static Glyph
GlyphForStock(const nsACString& aStock)
{
  static const struct { const char* name; Glyph glyph; } kGlyphs[] = {
    { "gtk-go-back-ltr", Glyph::ArrowLeft },
    { "gtk-go-forward-rtl", Glyph::ArrowLeft },
    { "gtk-go-back", Glyph::ArrowLeft },
    { "gtk-go-forward-ltr", Glyph::ArrowRight },
    { "gtk-go-back-rtl", Glyph::ArrowRight },
    { "gtk-go-forward", Glyph::ArrowRight },
    { "gtk-jump-to", Glyph::ArrowRight },
    { "gtk-go-up", Glyph::ArrowUp },
    { "gtk-go-down", Glyph::ArrowDown },
    { "gtk-close", Glyph::Cross },
    { "gtk-cancel", Glyph::Cross },
    { "gtk-clear", Glyph::Cross },
    { "gtk-delete", Glyph::Cross },
    { "gtk-no", Glyph::Cross },
    { "gtk-add", Glyph::Plus },
    { "gtk-zoom-in", Glyph::Plus },
    { "gtk-remove", Glyph::Minus },
    { "gtk-zoom-out", Glyph::Minus },
    { "gtk-refresh", Glyph::Refresh },
    { "gtk-undelete", Glyph::Undo },
    { "gtk-undo", Glyph::Undo },
    { "gtk-redo", Glyph::Redo },
    { "gtk-media-play", Glyph::Play },
    { "gtk-media-pause", Glyph::Pause },
    { "gtk-yes", Glyph::Check },
    { "gtk-apply", Glyph::Check },
    { "gtk-find", Glyph::Find },
    { "gtk-dialog-warning", Glyph::Warning },
    { "gtk-dialog-error", Glyph::Error },
    { "gtk-dialog-info", Glyph::Info },
    { "gtk-dialog-question", Glyph::Question },
  };
  for (const auto& entry : kGlyphs) {
    if (aStock.EqualsASCII(entry.name))
      return entry.glyph;
  }
  return Glyph::None;
}

static void
DrawArrow(BView* aView, float s, Glyph aGlyph)
{
  // A shaft and a triangular head pointing right, rotated as needed.
  const float m = s / 8, w = s / 6;
  BPoint head[3];
  auto set = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
    head[0] = BPoint(x1, y1); head[1] = BPoint(x2, y2); head[2] = BPoint(x3, y3);
  };
  switch (aGlyph) {
    case Glyph::ArrowLeft:
      set(m, s / 2, s / 2, m, s / 2, s - m);
      aView->FillRect(BRect(s / 2, s / 2 - w / 2, s - m, s / 2 + w / 2));
      break;
    case Glyph::ArrowRight:
      set(s - m, s / 2, s / 2, m, s / 2, s - m);
      aView->FillRect(BRect(m, s / 2 - w / 2, s / 2, s / 2 + w / 2));
      break;
    case Glyph::ArrowUp:
      set(s / 2, m, m, s / 2, s - m, s / 2);
      aView->FillRect(BRect(s / 2 - w / 2, s / 2, s / 2 + w / 2, s - m));
      break;
    default:
      set(s / 2, s - m, m, s / 2, s - m, s / 2);
      aView->FillRect(BRect(s / 2 - w / 2, m, s / 2 + w / 2, s / 2));
      break;
  }
  aView->FillPolygon(head, 3);
}

static void
DrawBadge(BView* aView, float s, rgb_color aColor, const char* aText,
          bool aTriangle)
{
  const float m = s / 16;
  aView->SetHighColor(aColor);
  if (aTriangle) {
    BPoint tri[3] = { BPoint(s / 2, m), BPoint(m, s - m), BPoint(s - m, s - m) };
    aView->FillPolygon(tri, 3);
  } else {
    aView->FillEllipse(BRect(m, m, s - m, s - m));
  }
  BFont font(be_bold_font);
  font.SetSize(s * (aTriangle ? 0.55f : 0.7f));
  aView->SetFont(&font);
  font_height fh;
  font.GetHeight(&fh);
  aView->SetHighColor(aTriangle ? make_color(0, 0, 0, 255)
                                : make_color(255, 255, 255, 255));
  const float x = (s - font.StringWidth(aText)) / 2;
  const float y = aTriangle ? s - m - s / 12 : (s + fh.ascent - fh.descent) / 2;
  aView->DrawString(aText, BPoint(x, y));
}

static bool
DrawStockGlyph(Glyph aGlyph, BBitmap* aDest, bool aDisabled)
{
  const BRect bounds = aDest->Bounds();
  BBitmap canvas(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
  if (canvas.InitCheck() != B_OK)
    return false;
  memset(canvas.Bits(), 0, canvas.BitsLength());
  BView* view = new BView(bounds, "glyph", B_FOLLOW_NONE, B_WILL_DRAW);
  canvas.AddChild(view);
  if (!canvas.Lock())
    return false;

  // Composite onto the transparent canvas so the result keeps its alpha.
  view->SetDrawingMode(B_OP_ALPHA);
  view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
  rgb_color ink = ui_color(B_MENU_ITEM_TEXT_COLOR);
  ink.alpha = 255;
  view->SetHighColor(ink);
  const float s = bounds.Width() + 1;
  const float m = s / 6, pen = std::max(1.5f, s / 8);
  view->SetPenSize(pen);
  view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

  switch (aGlyph) {
    case Glyph::ArrowLeft: case Glyph::ArrowRight:
    case Glyph::ArrowUp: case Glyph::ArrowDown:
      DrawArrow(view, s - 1, aGlyph);
      break;
    case Glyph::Cross:
      view->StrokeLine(BPoint(m, m), BPoint(s - 1 - m, s - 1 - m));
      view->StrokeLine(BPoint(s - 1 - m, m), BPoint(m, s - 1 - m));
      break;
    case Glyph::Plus:
      view->StrokeLine(BPoint(s / 2, m), BPoint(s / 2, s - 1 - m));
      // fall through
    case Glyph::Minus:
      view->StrokeLine(BPoint(m, s / 2), BPoint(s - 1 - m, s / 2));
      break;
    case Glyph::Refresh:
    case Glyph::Undo:
    case Glyph::Redo: {
      const BRect r(m, m, s - 1 - m, s - 1 - m);
      const bool left = aGlyph == Glyph::Undo;
      view->StrokeArc(r, left ? 120 : 330, aGlyph == Glyph::Refresh ? 270 : 230);
      // Arrow head at the start of the arc.
      const float hx = left ? m : s - 1 - m, hy = s / 2;
      BPoint tri[3] = { BPoint(hx - s / 5, hy - s / 8), BPoint(hx + s / 5, hy - s / 8),
                        BPoint(hx, hy + s / 6) };
      view->FillPolygon(tri, 3);
      break;
    }
    case Glyph::Play: {
      BPoint tri[3] = { BPoint(m, m), BPoint(s - 1 - m, s / 2), BPoint(m, s - 1 - m) };
      view->FillPolygon(tri, 3);
      break;
    }
    case Glyph::Pause:
      view->FillRect(BRect(m, m, s / 2 - s / 10, s - 1 - m));
      view->FillRect(BRect(s / 2 + s / 10, m, s - 1 - m, s - 1 - m));
      break;
    case Glyph::Check:
      view->StrokeLine(BPoint(m, s / 2), BPoint(s / 2.5f, s - 1 - m));
      view->StrokeLine(BPoint(s / 2.5f, s - 1 - m), BPoint(s - 1 - m, m));
      break;
    case Glyph::Find:
      view->StrokeEllipse(BRect(m, m, s * 0.62f, s * 0.62f));
      view->StrokeLine(BPoint(s * 0.6f, s * 0.6f), BPoint(s - 1 - m, s - 1 - m));
      break;
    case Glyph::Warning:
      DrawBadge(view, s - 1, make_color(255, 203, 0, ink.alpha), "!", true);
      break;
    case Glyph::Error:
      DrawBadge(view, s - 1, make_color(220, 30, 30, ink.alpha), "\xc3\x97", false);
      break;
    case Glyph::Info:
      DrawBadge(view, s - 1, make_color(50, 110, 210, ink.alpha), "i", false);
      break;
    case Glyph::Question:
      DrawBadge(view, s - 1, make_color(50, 110, 210, ink.alpha), "?", false);
      break;
    case Glyph::None:
      canvas.Unlock();
      return false;
  }
  view->Sync();
  canvas.Unlock();
  CopyBitmap(&canvas, aDest);
  if (aDisabled) {
    // Fade the finished glyph, so overlapping strokes stay even.
    const int32 rows = bounds.IntegerHeight() + 1;
    const int32 cols = bounds.IntegerWidth() + 1;
    for (int32 y = 0; y < rows; y++) {
      uint8* pixel = (uint8*)aDest->Bits() + y * aDest->BytesPerRow();
      for (int32 x = 0; x < cols; x++, pixel += 4)
        pixel[3] = pixel[3] * 2 / 5;
    }
  }
  return true;
}

nsresult
nsIconChannel::Init(nsIURI* aURI)
{
  nsCOMPtr<nsIMozIconURI> iconURI = do_QueryInterface(aURI);
  NS_ENSURE_TRUE(iconURI, NS_ERROR_INVALID_ARG);

  uint32_t iconSize = 16;
  nsAutoCString iconSizeString;
  iconURI->GetIconSize(iconSizeString);
  if (iconSizeString.IsEmpty()) {
    iconURI->GetImageSize(&iconSize);
  } else if (iconSizeString.EqualsLiteral("dialog")) {
    iconSize = 48;
  } else if (iconSizeString.EqualsLiteral("dnd")) {
    iconSize = 32;
  } else if (iconSizeString.EqualsLiteral("toolbar")) {
    iconSize = 24;
  }
  if (iconSize == 0 || iconSize > 255)
    iconSize = 16;
  const icon_size fallbackSize = iconSize <= 16 ? B_MINI_ICON : B_LARGE_ICON;

  BBitmap bitmap(BRect(0, 0, iconSize - 1, iconSize - 1), B_RGBA32);
  if (bitmap.InitCheck() != B_OK)
    return NS_ERROR_OUT_OF_MEMORY;
  memset(bitmap.Bits(), 0, bitmap.BitsLength());

  bool gotIcon = false;

  // A real file: use its Tracker icon.
  nsCOMPtr<nsIURL> iconURL;
  iconURI->GetIconURL(getter_AddRefs(iconURL));
  if (nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(iconURL)) {
    nsCOMPtr<nsIFile> file;
    nsAutoCString path;
    if (NS_SUCCEEDED(fileURL->GetFile(getter_AddRefs(file))) && file &&
        NS_SUCCEEDED(file->GetNativePath(path))) {
      BNode node(path.get());
      BNodeInfo info(&node);
      if (node.InitCheck() == B_OK && info.InitCheck() == B_OK)
        gotIcon = info.GetTrackerIcon(&bitmap, fallbackSize) == B_OK;
    }
  }

  // Otherwise the content type, or a type guessed from the extension.
  if (!gotIcon) {
    nsAutoCString type;
    iconURI->GetContentType(type);
    if (type.IsEmpty()) {
      nsAutoCString ext;
      iconURI->GetFileExtension(ext);
      if (!ext.IsEmpty()) {
        nsAutoCString name("file");
        if (ext.First() != '.')
          name.Append('.');
        name.Append(ext);
        BMimeType guessed;
        if (BMimeType::GuessMimeType(name.get(), &guessed) == B_OK)
          type.Assign(guessed.Type());
      }
    }
    if (!type.IsEmpty())
      gotIcon = GetMimeIcon(type.get(), &bitmap, fallbackSize);
  }

  // Stock icons: a Haiku icon where one fits, else a drawn glyph. A stock
  // icon we can't provide fails rather than showing a generic file icon.
  nsAutoCString stock;
  iconURI->GetStockIcon(stock);
  if (!gotIcon && !stock.IsEmpty()) {
    nsAutoCString state;
    iconURI->GetIconState(state);
    const bool disabled = state.EqualsLiteral("disabled");
    const char* type = nullptr;
    const char* systemIcon = nullptr;
    if (stock.EqualsLiteral("gtk-directory") || stock.EqualsLiteral("folder") ||
        stock.EqualsLiteral("gtk-open") || stock.EqualsLiteral("gtk-home"))
      type = "application/x-vnd.Be-directory";
    else if (stock.EqualsLiteral("gtk-file") || stock.EqualsLiteral("gtk-new"))
      type = B_FILE_MIME_TYPE;
    else if (stock.EqualsLiteral("drive-harddisk") ||
             stock.EqualsLiteral("gtk-harddisk"))
      type = "application/x-vnd.Be-volume";
    else if (stock.EqualsLiteral("gtk-print") ||
             stock.EqualsLiteral("gtk-print-preview"))
      type = "application/x-vnd.Be.printer";
    else if (stock.EqualsLiteral("gtk-dialog-warning"))
      systemIcon = "dialog-warning";
    else if (stock.EqualsLiteral("gtk-dialog-error"))
      systemIcon = "dialog-error";
    else if (stock.EqualsLiteral("gtk-dialog-info"))
      systemIcon = "dialog-information";
    // Only names Haiku has: GetSystemIcon() can hang on unknown ones
    // (e.g. "dialog-question" on R1/beta6), so the question mark is drawn.
    if (type)
      gotIcon = GetMimeIcon(type, &bitmap, fallbackSize);
    if (!gotIcon && systemIcon && !disabled)
      gotIcon = BIconUtils::GetSystemIcon(systemIcon, &bitmap) == B_OK;
    if (!gotIcon)
      gotIcon = DrawStockGlyph(GlyphForStock(stock), &bitmap, disabled);
    if (!gotIcon)
      return NS_ERROR_NOT_AVAILABLE;
  }

  // Last resort for files and types: the generic file icon.
  if (!gotIcon)
    gotIcon = GetMimeIcon(B_FILE_MIME_TYPE, &bitmap, fallbackSize);

  if (!gotIcon)
    return NS_ERROR_NOT_AVAILABLE;

  return BitmapToChannel(&bitmap, iconURI, getter_AddRefs(mRealChannel));
}
