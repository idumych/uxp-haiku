/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDragService.h"
#include "HaikuUtils.h"
#include "HaikuView.h"
#include "nsWindow.h"

#include <cstring>

#include "nsArrayUtils.h"
#include "nsIDOMNode.h"
#include "nsIDocument.h"
#include "nsIFile.h"
#include "nsIPresShell.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "imgIContainer.h"
#include "nsIWidget.h"
#include "nsNetUtil.h"
#include "nsPrimitiveHelpers.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsViewManager.h"
#include "nsWidgetsCID.h"
#include "nsXPIDLString.h"
#include "mozilla/MouseEvents.h"

using namespace mozilla;
using namespace mozilla::widget;

static NS_DEFINE_CID(kCDragServiceCID, NS_DRAGSERVICE_CID);

nsDragService::nsDragService() {}

nsDragService::~nsDragService() {}

nsDragService* nsDragService::GetInstance()
{
  nsCOMPtr<nsIDragService> service = do_GetService(kCDragServiceCID);
  return static_cast<nsDragService*>(service.get());
}

void nsDragService::SetDragMessage(UniquePtr<BMessage> aMessage)
{
  mDragMessage = std::move(aMessage);
}

// --- Reading an incoming drag message --------------------------------------

static int32 CountRefs(const BMessage* aMsg)
{
  type_code type;
  int32 count = 0;
  if (aMsg && aMsg->GetInfo("refs", &type, &count) == B_OK) {
    return count;
  }
  return 0;
}

static bool FindMimeString(const BMessage* aMsg, const char* aField,
                           nsACString& aOut)
{
  if (!aMsg) {
    return false;
  }
  const void* data = nullptr;
  ssize_t size = 0;
  if (aMsg->FindData(aField, B_MIME_TYPE, &data, &size) == B_OK && size > 0) {
    aOut.Assign(static_cast<const char*>(data), size);
  } else {
    const char* str = nullptr;
    if (aMsg->FindString(aField, &str) != B_OK || !str) {
      return false;
    }
    aOut.Assign(str);
  }
  // Strip a trailing NUL some apps include.
  if (!aOut.IsEmpty() && aOut.Last() == '\0') {
    aOut.Truncate(aOut.Length() - 1);
  }
  return !aOut.IsEmpty();
}

static bool FindURL(const BMessage* aMsg, nsACString& aURL)
{
  nsAutoCString list;
  if (FindMimeString(aMsg, "text/uri-list", list)) {
    int32_t nl = list.FindChar('\n');
    if (nl >= 0) {
      list.Truncate(nl);
    }
    list.Trim("\r\n ");
    aURL = list;
    return !aURL.IsEmpty();
  }
  return FindMimeString(aMsg, "be:url", aURL);
}

static already_AddRefed<nsIFile> FileForRef(const BMessage* aMsg,
                                            uint32_t aIndex)
{
  entry_ref ref;
  if (!aMsg || aMsg->FindRef("refs", aIndex, &ref) != B_OK) {
    return nullptr;
  }
  BPath path(&ref);
  if (path.InitCheck() != B_OK) {
    return nullptr;
  }
  nsCOMPtr<nsIFile> file;
  if (NS_FAILED(NS_NewNativeLocalFile(nsDependentCString(path.Path()), true,
                                      getter_AddRefs(file)))) {
    return nullptr;
  }
  return file.forget();
}

static bool MessageHasFlavor(const BMessage* aMsg, const char* aFlavor)
{
  nsAutoCString tmp;
  if (!strcmp(aFlavor, kFileMime)) {
    return CountRefs(aMsg) > 0;
  }
  if (!strcmp(aFlavor, kURLMime) || !strcmp(aFlavor, kURLDataMime)) {
    return FindURL(aMsg, tmp) || CountRefs(aMsg) > 0;
  }
  if (!strcmp(aFlavor, kUnicodeMime) || !strcmp(aFlavor, kTextMime)) {
    return FindMimeString(aMsg, "text/plain", tmp) || FindURL(aMsg, tmp);
  }
  if (!strcmp(aFlavor, kHTMLMime)) {
    return FindMimeString(aMsg, "text/html", tmp);
  }
  return false;
}

static void SetUnicodeData(nsITransferable* aTransferable, const char* aFlavor,
                           const nsAString& aText)
{
  nsCOMPtr<nsISupports> wrapper;
  uint32_t len = aText.Length() * 2;
  nsPrimitiveHelpers::CreatePrimitiveForData(aFlavor, aText.BeginReading(),
                                             len, getter_AddRefs(wrapper));
  if (wrapper) {
    aTransferable->SetTransferData(aFlavor, wrapper, len);
  }
}

static bool GetDataFromMessage(const BMessage* aMsg, nsITransferable* aTrans,
                               const char* aFlavor, uint32_t aIndex)
{
  nsAutoCString utf8;

  if (!strcmp(aFlavor, kFileMime)) {
    nsCOMPtr<nsIFile> file = FileForRef(aMsg, aIndex);
    if (!file) {
      return false;
    }
    aTrans->SetTransferData(aFlavor, file, sizeof(nsIFile*));
    return true;
  }

  if (!strcmp(aFlavor, kURLMime) || !strcmp(aFlavor, kURLDataMime)) {
    nsAutoCString url;
    if (CountRefs(aMsg) > 0) {
      nsCOMPtr<nsIFile> file = FileForRef(aMsg, aIndex);
      if (!file || NS_FAILED(NS_GetURLSpecFromFile(file, url))) {
        return false;
      }
    } else if (!FindURL(aMsg, url)) {
      return false;
    }
    NS_ConvertUTF8toUTF16 data(url);
    if (!strcmp(aFlavor, kURLMime)) {
      data.Append('\n');
      data.Append(NS_ConvertUTF8toUTF16(url));  // no title: reuse the URL
    }
    SetUnicodeData(aTrans, aFlavor, data);
    return true;
  }

  if (!strcmp(aFlavor, kUnicodeMime) || !strcmp(aFlavor, kTextMime)) {
    if (!FindMimeString(aMsg, "text/plain", utf8) && !FindURL(aMsg, utf8)) {
      return false;
    }
    SetUnicodeData(aTrans, kUnicodeMime, NS_ConvertUTF8toUTF16(utf8));
    return true;
  }

  if (!strcmp(aFlavor, kHTMLMime)) {
    if (!FindMimeString(aMsg, "text/html", utf8)) {
      return false;
    }
    SetUnicodeData(aTrans, kHTMLMime, NS_ConvertUTF8toUTF16(utf8));
    return true;
  }

  return false;
}

// --- nsIDragSession --------------------------------------------------------

NS_IMETHODIMP
nsDragService::GetNumDropItems(uint32_t* aNumItems)
{
  *aNumItems = 0;
  if (mSourceDataItems) {
    return mSourceDataItems->GetLength(aNumItems);
  }
  int32 refs = CountRefs(mDragMessage.get());
  *aNumItems = refs > 0 ? refs : (mDragMessage ? 1 : 0);
  return NS_OK;
}

NS_IMETHODIMP
nsDragService::GetData(nsITransferable* aTransferable, uint32_t aItemIndex)
{
  NS_ENSURE_ARG(aTransferable);

  nsCOMPtr<nsIArray> flavorList;
  nsresult rv =
      aTransferable->FlavorsTransferableCanImport(getter_AddRefs(flavorList));
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t count = 0;
  flavorList->GetLength(&count);

  for (uint32_t i = 0; i < count; ++i) {
    nsCOMPtr<nsISupportsCString> flavorWrapper =
        do_QueryElementAt(flavorList, i);
    if (!flavorWrapper) {
      continue;
    }
    nsXPIDLCString flavor;
    flavorWrapper->ToString(getter_Copies(flavor));

    if (mSourceDataItems) {
      // Our own drag: copy straight from the source transferable.
      nsCOMPtr<nsITransferable> item =
          do_QueryElementAt(mSourceDataItems, aItemIndex);
      if (!item) {
        continue;
      }
      nsCOMPtr<nsISupports> data;
      uint32_t len = 0;
      if (NS_SUCCEEDED(item->GetTransferData(flavor, getter_AddRefs(data),
                                             &len)) &&
          NS_SUCCEEDED(aTransferable->SetTransferData(flavor, data, len))) {
        return NS_OK;
      }
      continue;
    }

    if (GetDataFromMessage(mDragMessage.get(), aTransferable, flavor,
                           aItemIndex)) {
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDragService::IsDataFlavorSupported(const char* aDataFlavor, bool* _retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = false;
  if (!aDataFlavor) {
    return NS_ERROR_INVALID_ARG;
  }

  if (mSourceDataItems) {
    uint32_t numItems = 0;
    mSourceDataItems->GetLength(&numItems);
    for (uint32_t i = 0; i < numItems && !*_retval; ++i) {
      nsCOMPtr<nsITransferable> item = do_QueryElementAt(mSourceDataItems, i);
      nsCOMPtr<nsIArray> flavors;
      if (!item ||
          NS_FAILED(item->FlavorsTransferableCanExport(getter_AddRefs(flavors)))) {
        continue;
      }
      uint32_t n = 0;
      flavors->GetLength(&n);
      for (uint32_t j = 0; j < n; ++j) {
        nsCOMPtr<nsISupportsCString> f = do_QueryElementAt(flavors, j);
        nsXPIDLCString name;
        if (f && NS_SUCCEEDED(f->ToString(getter_Copies(name))) &&
            !strcmp(name, aDataFlavor)) {
          *_retval = true;
          break;
        }
      }
    }
    return NS_OK;
  }

  *_retval = MessageHasFlavor(mDragMessage.get(), aDataFlavor);
  return NS_OK;
}

NS_IMETHODIMP
nsDragService::EndDragSession(bool aDoneDrag)
{
  nsresult rv = nsBaseDragService::EndDragSession(aDoneDrag);
  mSourceDataItems = nullptr;
  mDragMessage = nullptr;
  return rv;
}

// --- Outgoing drags ----------------------------------------------------------

static bool GetTransferString(nsITransferable* aItem, const char* aFlavor,
                              nsAString& aOut)
{
  nsCOMPtr<nsISupports> data;
  uint32_t len = 0;
  if (NS_FAILED(aItem->GetTransferData(aFlavor, getter_AddRefs(data), &len))) {
    return false;
  }
  nsCOMPtr<nsISupportsString> str = do_QueryInterface(data);
  if (!str) {
    return false;
  }
  str->GetData(aOut);
  return !aOut.IsEmpty();
}

nsresult
nsDragService::InvokeDragSessionImpl(nsIArray* aTransferableArray,
                                     nsIScriptableRegion* aDragRgn,
                                     uint32_t aActionType)
{
  NS_ENSURE_ARG(aTransferableArray);

  // Find the source BView through the source document's widget.
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(mSourceDocument);
  nsIPresShell* shell = doc ? doc->GetShell() : nullptr;
  nsViewManager* vm = shell ? shell->GetViewManager() : nullptr;
  nsCOMPtr<nsIWidget> widget;
  if (vm) {
    vm->GetRootWidget(getter_AddRefs(widget));
  }
  BView* view = widget
      ? static_cast<BView*>(widget->GetNativeData(NS_NATIVE_WIDGET)) : nullptr;
  if (!view || !view->Window()) {
    return NS_ERROR_FAILURE;
  }

  BMessage msg(B_SIMPLE_DATA);
  uint32_t numItems = 0;
  aTransferableArray->GetLength(&numItems);
  for (uint32_t i = 0; i < numItems; ++i) {
    nsCOMPtr<nsITransferable> item = do_QueryElementAt(aTransferableArray, i);
    if (!item) {
      continue;
    }

    nsCOMPtr<nsISupports> data;
    uint32_t len = 0;
    if (NS_SUCCEEDED(item->GetTransferData(kFileMime, getter_AddRefs(data),
                                           &len))) {
      nsCOMPtr<nsIFile> file = do_QueryInterface(data);
      nsAutoCString path;
      entry_ref ref;
      if (file && NS_SUCCEEDED(file->GetNativePath(path)) &&
          get_ref_for_path(path.get(), &ref) == B_OK) {
        msg.AddRef("refs", &ref);
      }
    }

    nsAutoString str;
    if (GetTransferString(item, kURLMime, str)) {
      int32_t nl = str.FindChar('\n');
      if (nl >= 0) {
        str.Truncate(nl);
      }
      NS_ConvertUTF16toUTF8 url(str);
      msg.AddData("text/uri-list", B_MIME_TYPE, url.get(), url.Length());
      msg.AddString("be:url", url.get());
      if (!msg.HasData("text/plain", B_MIME_TYPE)) {
        msg.AddData("text/plain", B_MIME_TYPE, url.get(), url.Length());
      }
    }
    if (GetTransferString(item, kUnicodeMime, str)) {
      NS_ConvertUTF16toUTF8 text(str);
      msg.RemoveName("text/plain");
      msg.AddData("text/plain", B_MIME_TYPE, text.get(), text.Length());
    }
    if (GetTransferString(item, kHTMLMime, str)) {
      NS_ConvertUTF16toUTF8 html(str);
      msg.AddData("text/html", B_MIME_TYPE, html.get(), html.Length());
    }
  }

  // Offer Tracker a file through the negotiated drag protocol
  // (B_COPY_TARGET): an image as a PNG file, or a link as a bookmark file.
  // HaikuView writes the file when Tracker replies with the target folder;
  // everything it needs travels in "be:originator-data".
  nsCOMPtr<nsITransferable> first = do_QueryElementAt(aTransferableArray, 0);
  if (first) {
    BMessage origin;
    nsAutoString name;
    GetTransferString(first, kFilePromiseDestFilename, name);

    nsCOMPtr<nsISupports> data;
    uint32_t len = 0;
    nsCOMPtr<imgIContainer> image;
    if (NS_SUCCEEDED(first->GetTransferData(kNativeImageMime,
                                            getter_AddRefs(data), &len))) {
      nsCOMPtr<nsISupportsInterfacePointer> ptr = do_QueryInterface(data);
      nsCOMPtr<nsISupports> primitive;
      if (ptr) {
        ptr->GetData(getter_AddRefs(primitive));
      }
      image = do_QueryInterface(primitive ? primitive : data);
    }

    BMallocIO png;
    BBitmap* bitmap = image ? CreateBBitmapFromImage(image) : nullptr;
    if (bitmap && EncodeBitmapAsPNG(bitmap, png)) {
      origin.AddData("image/png", B_MIME_TYPE, png.Buffer(),
                     png.BufferLength());
      msg.AddString("be:filetypes", "image/png");
      msg.AddString("be:type_descriptions", "PNG image");
      if (name.IsEmpty()) {
        name.AssignLiteral("image.png");
      }
    } else {
      nsAutoString urlAndTitle;
      if (GetTransferString(first, kURLMime, urlAndTitle)) {
        int32_t nl = urlAndTitle.FindChar('\n');
        nsAutoString url(nl >= 0 ? nsAutoString(Substring(urlAndTitle, 0, nl))
                                 : urlAndTitle);
        nsAutoString title(nl >= 0 ? nsAutoString(Substring(urlAndTitle, nl + 1))
                                   : url);
        origin.AddString("url", NS_ConvertUTF16toUTF8(url).get());
        origin.AddString("title", NS_ConvertUTF16toUTF8(title).get());
        msg.AddString("be:filetypes", "application/x-vnd.Be-bookmark");
        msg.AddString("be:type_descriptions", "Bookmark");
        if (name.IsEmpty()) {
          name = title;
        }
      }
    }

    if (!origin.IsEmpty()) {
      // File names can't contain '/'.
      name.ReplaceChar('/', '-');
      if (name.IsEmpty()) {
        name.AssignLiteral("Untitled");
      }
      msg.AddString("be:types", B_FILE_MIME_TYPE);
      msg.AddString("be:clip_name", NS_ConvertUTF16toUTF8(name).get());
      msg.AddMessage("be:originator-data", &origin);
      msg.AddInt32("be:actions", B_COPY_TARGET);
    }
  }

  if (msg.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }
  if (!msg.HasInt32("be:actions")) {
    msg.AddInt32("be:actions", (aActionType & DRAGDROP_ACTION_MOVE)
                                   ? B_MOVE_TARGET : B_COPY_TARGET);
  }

  mSourceDataItems = aTransferableArray;

  nsresult rv = StartDragSession();
  if (NS_FAILED(rv)) {
    mSourceDataItems = nullptr;
    return rv;
  }

  BWindow* window = view->Window();
  if (window->Lock()) {
    BPoint where;
    uint32 buttons = 0;
    view->GetMouse(&where, &buttons, false);
    // Replies (B_COPY_TARGET) go to the view.
    view->DragMessage(&msg, BRect(where.x - 16, where.y - 16, where.x + 16,
                                  where.y + 16), view);
    window->Unlock();
  }
  return NS_OK;
}
