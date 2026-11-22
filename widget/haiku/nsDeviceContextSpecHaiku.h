/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDeviceContextSpecHaiku_h___
#define nsDeviceContextSpecHaiku_h___

#include "nsIDeviceContextSpec.h"
#include "nsIPrinterEnumerator.h"
#include "nsIPrintSettings.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsTArray.h"

class BBitmap;

/*
 * Printing on Haiku.
 *
 * Print to file: cairo's PDF backend writes the file (PrintTargetPDF).
 * Print to a printer: each page is rendered by cairo into an image surface
 * at kPrintDPI, kept as a BBitmap, and at the end of the document sent to
 * the Haiku print server with BPrintJob (which shows Haiku's job dialog, so
 * the user picks printer, copies and range there).
 */
class nsDeviceContextSpecHaiku final : public nsIDeviceContextSpec {
 public:
  nsDeviceContextSpecHaiku();

  NS_DECL_ISUPPORTS

  NS_IMETHOD Init(nsIWidget* aWidget, nsIPrintSettings* aPS,
                  bool aIsPrintPreview) override;
  already_AddRefed<PrintTarget> MakePrintTarget() override;
  float GetPrintingScale() override;
  NS_IMETHOD BeginDocument(const nsAString& aTitle,
                           const nsAString& aPrintToFileName,
                           int32_t aStartPage, int32_t aEndPage) override;
  NS_IMETHOD EndDocument() override;
  NS_IMETHOD AbortDocument() override;
  NS_IMETHOD BeginPage() override { return NS_OK; }
  NS_IMETHOD EndPage() override { return NS_OK; }

  // Called by the raster print target at the end of each page.
  void AddPage(BBitmap* aPage) { mPages.AppendElement(aPage); }

 private:
  ~nsDeviceContextSpecHaiku();
  void ClearPages();
  bool PrintToFile();

  nsCOMPtr<nsIPrintSettings> mPrintSettings;
  nsString mTitle;
  nsTArray<BBitmap*> mPages;
  bool mIsPrintPreview;
};

class nsPrinterEnumeratorHaiku final : public nsIPrinterEnumerator {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPRINTERENUMERATOR

 private:
  ~nsPrinterEnumeratorHaiku() = default;
};

#endif  // nsDeviceContextSpecHaiku_h___
