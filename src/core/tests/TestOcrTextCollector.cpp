// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <boost/test/unit_test.hpp>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <memory>

#include "Dpi.h"
#include "FileNameDisambiguator.h"
#include "ImageMetadata.h"
#include "OutputFileNameGenerator.h"
#include "PageInfo.h"
#include "PageSequence.h"
#include "PdfExporter.h"
#include "filters/ocr/OcrResult.h"
#include "filters/ocr/OcrTextCollector.h"
#include "filters/ocr/Settings.h"

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace {

constexpr int kImageWidth = 400;
constexpr int kImageHeight = 600;

OutputFileNameGenerator makeGenerator(const QString& outDir) {
  OutputFileNameGenerator gen(std::make_shared<FileNameDisambiguator>(), outDir, Qt::LeftToRight);
  gen.setOutputFormat(OutputImageFormat::PNG);
  return gen;
}

PageInfo makePage(const QString& sourcePath) {
  const PageId pageId(ImageId(sourcePath, 0), PageId::SINGLE_PAGE);
  const ImageMetadata meta(QSize(kImageWidth, kImageHeight), Dpi(300, 300));
  return PageInfo(pageId, meta, 1, false, false);
}

// Writes the page's processed-output stand-in where the generator expects it.
QString saveOutputImage(const OutputFileNameGenerator& gen, const PageId& pageId) {
  const QString path = gen.filePathFor(pageId);
  QImage image(QSize(kImageWidth, kImageHeight), QImage::Format_RGB32);
  image.fill(Qt::white);
  BOOST_REQUIRE(image.save(path));
  return path;
}

ocr::OcrResult makeOcrResult() {
  ocr::OcrResult result;
  result.setImageDimensions(kImageWidth, kImageHeight);
  result.addWord(ocr::OcrWord("Recognized", QRectF(20, 30, 200, 40), 0.95f));
  result.addWord(ocr::OcrWord("text", QRectF(20, 90, 100, 40), 0.9f));
  return result;
}

#ifdef Q_OS_MACOS
void appendShownTextLength(CGPDFScannerRef scanner, void* info) {
  CGPDFStringRef string = nullptr;
  if (CGPDFScannerPopString(scanner, &string)) {
    *static_cast<size_t*>(info) += CGPDFStringGetLength(string);
  }
}

void appendShownTextArrayLength(CGPDFScannerRef scanner, void* info) {
  CGPDFArrayRef array = nullptr;
  if (!CGPDFScannerPopArray(scanner, &array)) {
    return;
  }
  for (size_t i = 0; i < CGPDFArrayGetCount(array); ++i) {
    CGPDFStringRef string = nullptr;
    if (CGPDFArrayGetString(array, i, &string)) {
      *static_cast<size_t*>(info) += CGPDFStringGetLength(string);
    }
  }
}

size_t pdfPageTextLength(const QString& pdfPath, size_t pageNumber) {
  const QByteArray encodedPath = QFile::encodeName(pdfPath);
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault,
      reinterpret_cast<const UInt8*>(encodedPath.constData()),
      encodedPath.size(),
      false);
  CGPDFDocumentRef document = CGPDFDocumentCreateWithURL(url);
  CFRelease(url);
  BOOST_REQUIRE(document != nullptr);

  CGPDFPageRef page = CGPDFDocumentGetPage(document, pageNumber);
  BOOST_REQUIRE(page != nullptr);

  size_t textLength = 0;
  CGPDFOperatorTableRef operatorTable = CGPDFOperatorTableCreate();
  CGPDFOperatorTableSetCallback(operatorTable, "Tj", &appendShownTextLength);
  CGPDFOperatorTableSetCallback(operatorTable, "'", &appendShownTextLength);
  CGPDFOperatorTableSetCallback(operatorTable, "\"", &appendShownTextLength);
  CGPDFOperatorTableSetCallback(operatorTable, "TJ", &appendShownTextArrayLength);

  CGPDFContentStreamRef contentStream = CGPDFContentStreamCreateWithPage(page);
  CGPDFScannerRef scanner = CGPDFScannerCreate(contentStream, operatorTable, &textLength);
  CGPDFScannerScan(scanner);
  CGPDFScannerRelease(scanner);
  CGPDFContentStreamRelease(contentStream);
  CGPDFOperatorTableRelease(operatorTable);
  CGPDFDocumentRelease(document);
  return textLength;
}
#endif  // Q_OS_MACOS

}  // namespace

BOOST_AUTO_TEST_SUITE(OcrTextCollectorTestSuite)

// Pins the Auto Process export bug: OCR results stored during a batch must
// reach the exported PDF even when the OCR stage toggle reads disabled at
// export time (as after loading a project whose XML lacks the flag).
BOOST_AUTO_TEST_CASE(collects_stored_results_even_when_ocr_stage_disabled) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const OutputFileNameGenerator gen = makeGenerator(dir.path());

  const PageInfo page = makePage(dir.filePath("scan.png"));
  const QString outputPath = saveOutputImage(gen, page.id());

  PageSequence pages;
  pages.append(page);

  ocr::Settings settings;
  settings.setOcrEnabled(false);
  settings.setOcrResult(page.id(), makeOcrResult());

  const QMap<QString, PdfExporter::OcrTextData> ocrData = ocr::collectOcrTextData(pages, gen, settings);

  BOOST_REQUIRE_EQUAL(ocrData.size(), 1);
  BOOST_REQUIRE(ocrData.contains(outputPath));
  const PdfExporter::OcrTextData& textData = ocrData.value(outputPath);
  BOOST_CHECK_EQUAL(textData.words.size(), 2);
  BOOST_CHECK_EQUAL(textData.imageWidth, kImageWidth);
  BOOST_CHECK_EQUAL(textData.imageHeight, kImageHeight);
}

BOOST_AUTO_TEST_CASE(skips_pages_without_results_or_without_output_files) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const OutputFileNameGenerator gen = makeGenerator(dir.path());

  // Output file exists but no OCR result was stored.
  const PageInfo processedPage = makePage(dir.filePath("processed.png"));
  saveOutputImage(gen, processedPage.id());

  // OCR result stored but the page was never processed to an output file.
  const PageInfo unprocessedPage = makePage(dir.filePath("unprocessed.png"));

  PageSequence pages;
  pages.append(processedPage);
  pages.append(unprocessedPage);

  ocr::Settings settings;
  settings.setOcrResult(unprocessedPage.id(), makeOcrResult());

  const QMap<QString, PdfExporter::OcrTextData> ocrData = ocr::collectOcrTextData(pages, gen, settings);
  BOOST_CHECK(ocrData.isEmpty());
}

#ifdef Q_OS_MACOS
BOOST_AUTO_TEST_CASE(exported_pdf_page_has_text_after_ocr_batch) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const OutputFileNameGenerator gen = makeGenerator(dir.path());

  const PageInfo page = makePage(dir.filePath("scan.png"));
  const QString outputPath = saveOutputImage(gen, page.id());

  PageSequence pages;
  pages.append(page);

  ocr::Settings settings;
  settings.setOcrEnabled(false);
  settings.setOcrResult(page.id(), makeOcrResult());

  const QMap<QString, PdfExporter::OcrTextData> ocrData = ocr::collectOcrTextData(pages, gen, settings);

  const QString pdfPath = dir.filePath("with-text-layer.pdf");
  BOOST_REQUIRE(PdfExporter::exportToPdf({outputPath}, pdfPath, "OCR test", "ScanTailor",
                                         PdfExporter::Quality::High, false, 0, ocrData));
  BOOST_CHECK_GT(pdfPageTextLength(pdfPath, 1), 0u);

  // Control: without OCR data the page carries no text, so the assertion
  // above cannot pass vacuously.
  const QString barePdfPath = dir.filePath("without-text-layer.pdf");
  BOOST_REQUIRE(PdfExporter::exportToPdf({outputPath}, barePdfPath, "OCR test", "ScanTailor",
                                         PdfExporter::Quality::High, false, 0));
  BOOST_CHECK_EQUAL(pdfPageTextLength(barePdfPath, 1), 0u);
}
#endif  // Q_OS_MACOS

BOOST_AUTO_TEST_SUITE_END()
