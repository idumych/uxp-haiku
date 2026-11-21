/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIconChannel.h"

#include <stdlib.h>
#include <string.h>

#pragma GCC visibility push(default)
#include <Bitmap.h>
#include <Entry.h>
#include <IconUtils.h>
#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
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

  // Stock icons we know a Haiku equivalent for.
  if (!gotIcon) {
    nsAutoCString stock;
    iconURI->GetStockIcon(stock);
    const char* type = nullptr;
    if (stock.EqualsLiteral("gtk-directory") || stock.EqualsLiteral("folder"))
      type = "application/x-vnd.Be-directory";
    else if (stock.EqualsLiteral("user-trash") || stock.EqualsLiteral("trash"))
      type = "application/x-vnd.Be-trash";
    else if (stock.EqualsLiteral("drive-harddisk"))
      type = "application/x-vnd.Be-volume";
    if (type)
      gotIcon = GetMimeIcon(type, &bitmap, fallbackSize);
  }

  // Last resort: the generic file icon.
  if (!gotIcon)
    gotIcon = GetMimeIcon(B_FILE_MIME_TYPE, &bitmap, fallbackSize);

  if (!gotIcon)
    return NS_ERROR_NOT_AVAILABLE;

  return BitmapToChannel(&bitmap, iconURI, getter_AddRefs(mRealChannel));
}
