/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDeviceContextSpecHaiku.h"

#include "HaikuPrintView.h"
#include "HaikuRemote.h"
#include "HaikuUtils.h"

#include <cstring>

#include "cairo.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PrintTarget.h"
#include "mozilla/gfx/PrintTargetPDF.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsComponentManagerUtils.h"
#include "nsStringEnumerator.h"
#include "nsXPCOM.h"
#include "nsCRTGlue.h"
#include "nsCoord.h"
#include "nsXPIDLString.h"

#include <algorithm>

#pragma GCC visibility push(default)
#include <interface/PrintJob.h>
#pragma GCC visibility pop

using namespace mozilla;
using namespace mozilla::gfx;

// Raster resolution for printer output: pages are drawn at
// kPrintScale x 72 dpi.
static const float kPrintScale = 2.0f;  // 144 dpi

// Raster print target: one cairo image surface reused for every page.
class PrintTargetHaiku final : public PrintTarget {
 public:
  static already_AddRefed<PrintTargetHaiku> Create(
      const IntSize& aPointSize, nsDeviceContextSpecHaiku* aSpec) {
    IntSize pixels(int32_t(aPointSize.width * kPrintScale),
                   int32_t(aPointSize.height * kPrintScale));
    cairo_surface_t* surface = cairo_image_surface_create(
        CAIRO_FORMAT_RGB24, pixels.width, pixels.height);
    if (cairo_surface_status(surface)) {
      cairo_surface_destroy(surface);
      return nullptr;
    }
    RefPtr<PrintTargetHaiku> target =
        new PrintTargetHaiku(surface, aPointSize, pixels, aSpec);
    return target.forget();
  }

  already_AddRefed<DrawTarget> MakeDrawTarget(
      const IntSize& aSize, DrawEventRecorder* aRecorder) override {
    // nsDeviceContext scales by GetPrintingScale(); draw on the whole
    // high-resolution surface.
    return PrintTarget::MakeDrawTarget(mPixelSize, aRecorder);
  }

  nsresult BeginPage() override {
    PrintTarget::BeginPage();
    // Start every page on white paper.
    cairo_t* cr = cairo_create(mCairoSurface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    return NS_OK;
  }

  nsresult EndPage() override {
    cairo_surface_flush(mCairoSurface);
    BBitmap* page = new BBitmap(
        BRect(0, 0, mPixelSize.width - 1, mPixelSize.height - 1), B_RGB32);
    if (page->InitCheck() == B_OK) {
      const uint8_t* src = cairo_image_surface_get_data(mCairoSurface);
      int32_t srcStride = cairo_image_surface_get_stride(mCairoSurface);
      uint8_t* dst = static_cast<uint8_t*>(page->Bits());
      int32_t dstStride = page->BytesPerRow();
      for (int32_t y = 0; y < mPixelSize.height; ++y) {
        memcpy(dst + y * dstStride, src + y * srcStride,
               mPixelSize.width * 4);
      }
      mSpec->AddPage(page);
    } else {
      delete page;
    }
    return PrintTarget::EndPage();
  }

 private:
  PrintTargetHaiku(cairo_surface_t* aSurface, const IntSize& aPointSize,
                   const IntSize& aPixelSize, nsDeviceContextSpecHaiku* aSpec)
      : PrintTarget(aSurface, aPointSize),
        mPixelSize(aPixelSize),
        mSpec(aSpec) {}

  IntSize mPixelSize;
  RefPtr<nsDeviceContextSpecHaiku> mSpec;
};

NS_IMPL_ISUPPORTS(nsDeviceContextSpecHaiku, nsIDeviceContextSpec)

nsDeviceContextSpecHaiku::nsDeviceContextSpecHaiku()
    : mIsPrintPreview(false) {}

nsDeviceContextSpecHaiku::~nsDeviceContextSpecHaiku() { ClearPages(); }

void nsDeviceContextSpecHaiku::ClearPages() {
  for (BBitmap* page : mPages) {
    delete page;
  }
  mPages.Clear();
}

NS_IMETHODIMP
nsDeviceContextSpecHaiku::Init(nsIWidget* aWidget, nsIPrintSettings* aPS,
                               bool aIsPrintPreview) {
  mPrintSettings = aPS;
  mIsPrintPreview = aIsPrintPreview;
  if (mPrintSettings) {
    mPrintSettings->SetIsInitializedFromPrinter(true);
  }
  return NS_OK;
}

bool nsDeviceContextSpecHaiku::PrintToFile() {
  bool toFile = false;
  if (mPrintSettings) {
    mPrintSettings->GetPrintToFile(&toFile);
  }
  return toFile;
}

float nsDeviceContextSpecHaiku::GetPrintingScale() {
  // Preview and PDF draw in points; printer output is rasterised.
  return (mIsPrintPreview || PrintToFile()) ? 1.0f : kPrintScale;
}

already_AddRefed<PrintTarget> nsDeviceContextSpecHaiku::MakePrintTarget() {
  NS_ENSURE_TRUE(mPrintSettings, nullptr);

  double width, height;
  mPrintSettings->GetEffectivePageSize(&width, &height);
  // twips to points
  IntSize size(int32_t(width / TWIPS_PER_POINT_FLOAT),
               int32_t(height / TWIPS_PER_POINT_FLOAT));

  if (mIsPrintPreview || PrintToFile()) {
    nsCOMPtr<nsIFile> file;
    nsXPIDLString fileName;
    mPrintSettings->GetToFileName(getter_Copies(fileName));
    nsresult rv = NS_NewLocalFile(fileName, false, getter_AddRefs(file));
    nsCOMPtr<nsIFileOutputStream> stream =
        do_CreateInstance("@mozilla.org/network/file-output-stream;1");
    if (NS_FAILED(rv) || !stream ||
        NS_FAILED(stream->Init(file, -1, -1, 0))) {
      return nullptr;
    }
    return PrintTargetPDF::CreateOrNull(stream, size);
  }

  ClearPages();
  RefPtr<PrintTargetHaiku> target = PrintTargetHaiku::Create(size, this);
  return target.forget();
}

NS_IMETHODIMP
nsDeviceContextSpecHaiku::BeginDocument(const nsAString& aTitle,
                                        const nsAString& aPrintToFileName,
                                        int32_t aStartPage,
                                        int32_t aEndPage) {
  mTitle = aTitle;
  return NS_OK;
}

NS_IMETHODIMP
nsDeviceContextSpecHaiku::EndDocument() {
  if (mIsPrintPreview || PrintToFile() || mPages.IsEmpty()) {
    ClearPages();
    return NS_OK;
  }

  mozilla::widget::EnsureHaikuApplication();
  NS_ConvertUTF16toUTF8 title(mTitle);
  BPrintJob job(title.IsEmpty() ? mozilla::widget::HaikuAppName()
                                : title.get());

  // Haiku's job dialog: printer, copies, page range.
  if (job.ConfigJob() != B_OK) {
    ClearPages();
    return NS_ERROR_ABORT;
  }

  BRect paper = job.PaperRect();
  BRect printable = job.PrintableRect();
  int32 first = std::max<int32>(job.FirstPage(), 1);
  int32 last = std::min<int32>(job.LastPage(), int32(mPages.Length()));

  job.BeginJob();
  if (!job.CanContinue()) {
    ClearPages();
    return NS_ERROR_FAILURE;
  }

  BRect viewFrame(0, 0, printable.Width(), printable.Height());
  BWindow* window = new BWindow(viewFrame, "print", B_NO_BORDER_WINDOW_LOOK,
                                B_NORMAL_WINDOW_FEEL, B_AVOID_FOCUS);
  BView* view = mozilla::widget::CreatePrintPageView(viewFrame);
  window->AddChild(view);
  window->Hide();
  window->Show();  // starts the looper; the window stays hidden

  // The page bitmap covers the paper; place it relative to the printable
  // area's origin.
  BRect dest(paper.left - printable.left, paper.top - printable.top,
             paper.right - printable.left, paper.bottom - printable.top);

  for (int32 i = first; i <= last && job.CanContinue(); ++i) {
    if (window->Lock()) {
      mozilla::widget::SetPrintPage(view, mPages[i - 1], dest);
      job.DrawView(view, viewFrame, BPoint(0, 0));
      window->Unlock();
    }
    job.SpoolPage();
  }

  if (job.CanContinue()) {
    job.CommitJob();
  }

  if (window->Lock()) {
    window->Quit();
  }
  ClearPages();
  return NS_OK;
}

NS_IMETHODIMP
nsDeviceContextSpecHaiku::AbortDocument() {
  ClearPages();
  return NS_OK;
}

// ---------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsPrinterEnumeratorHaiku, nsIPrinterEnumerator)

// Haiku chooses the printer in its own job dialog (BPrintJob::ConfigJob), so
// Gecko only sees one logical printer.
static const char16_t kHaikuPrinterName[] = u"Haiku printer";

NS_IMETHODIMP
nsPrinterEnumeratorHaiku::GetDefaultPrinterName(char16_t** aName) {
  *aName = NS_strdup(kHaikuPrinterName);
  return NS_OK;
}

NS_IMETHODIMP
nsPrinterEnumeratorHaiku::InitPrintSettingsFromPrinter(
    const char16_t* aPrinterName, nsIPrintSettings* aPrintSettings) {
  NS_ENSURE_ARG_POINTER(aPrintSettings);
  aPrintSettings->SetIsInitializedFromPrinter(true);
  return NS_OK;
}

NS_IMETHODIMP
nsPrinterEnumeratorHaiku::GetPrinterNameList(nsIStringEnumerator** aList) {
  nsTArray<nsString>* names = new nsTArray<nsString>();
  names->AppendElement(nsDependentString(kHaikuPrinterName));
  return NS_NewAdoptingStringEnumerator(aList, names);
}
