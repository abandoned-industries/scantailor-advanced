// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Stage 1.5.3 characterization: frozen v4 project fixtures.
//
// The committed fixtures under fixtures/projects/ were written once by
// project_fixture_writer (provenance in the README there) and are FROZEN: a
// future build that can no longer read them the same way fails here. Tests
// always operate on temp COPIES; the fixture bytes must never change.
//
// The pdf600-project fixture pins the F1 defect (audit F1, prereqs Part 2b)
// and, since QW5, its fix. The fixture was frozen BEFORE the pdfImportDpi
// file attribute existed, so opening it still renders at PdfReader's 300-DPI
// default while the stored metadata says 1200x1800 — the trigger for
// LoadFileTask::updateImageSizeIfChanged's silent metadata rewrite. That
// legacy-absence behavior is pinned explicitly (the code must handle a
// missing attribute with the old 300 default). The QW5 test then drives the
// fixed flow on a temp copy: set the import DPI as the import dialog would,
// resave (the attribute is emitted), reopen (the attribute restores the DPI
// map before any render), and the size mismatch is gone.

#include <ImageLoader.h>
#include <PdfReader.h>
#include <ProjectReader.h>
#include <StageSequence.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#include "ImageFileInfo.h"
#include "ImageMetadata.h"
#include "PageInfo.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "RoundtripProjectBuilder.h"
#include "filters/deskew/Filter.h"
#include "filters/deskew/Params.h"
#include "filters/deskew/Settings.h"
#include "filters/export/Filter.h"
#include "filters/export/Settings.h"
#include "filters/finalize/Filter.h"
#include "filters/finalize/Settings.h"
#include "filters/ocr/Filter.h"
#include "filters/ocr/OcrResult.h"
#include "filters/ocr/Settings.h"
#include "filters/output/Filter.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"
#include "filters/page_box/Filter.h"
#include "filters/page_box/Params.h"
#include "filters/page_box/Settings.h"
#include "filters/select_content/Filter.h"
#include "filters/select_content/Params.h"
#include "filters/select_content/Settings.h"
#include "zones/ZoneSet.h"

namespace Tests {
namespace {

QString fixturesRoot() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/projects");
}

bool copyDirRecursively(const QString& sourcePath, const QString& targetPath) {
  QDir source(sourcePath);
  if (!source.exists()) {
    return false;
  }
  QDir().mkpath(targetPath);
  for (const QFileInfo& entry : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString target = targetPath + QLatin1Char('/') + entry.fileName();
    if (entry.isDir()) {
      if (!copyDirRecursively(entry.absoluteFilePath(), target)) {
        return false;
      }
    } else if (!QFile::copy(entry.absoluteFilePath(), target)) {
      return false;
    }
  }
  return true;
}

QByteArray readBytes(const QString& path) {
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

QDomDocument loadDoc(const QString& projectFile) {
  QDomDocument doc;
  BOOST_REQUIRE(doc.setContent(readBytes(projectFile)));
  return doc;
}

struct OpenedFixture {
  QTemporaryDir temp;
  QString projectFile;
  QByteArray frozenBytes;
  std::unique_ptr<ProjectReader> reader;
  std::unique_ptr<StageSequence> stages;

  explicit OpenedFixture(const char* name) {
    BOOST_REQUIRE(temp.isValid());
    const QString source = fixturesRoot() + QLatin1Char('/') + QLatin1String(name);
    BOOST_REQUIRE_MESSAGE(QDir(source).exists(), "missing fixture: " << source.toStdString());
    BOOST_REQUIRE(copyDirRecursively(source, temp.path()));
    projectFile = temp.path() + QStringLiteral("/project.ScanTailor");
    frozenBytes = readBytes(QString(source) + QStringLiteral("/project.ScanTailor"));

    reader = std::make_unique<ProjectReader>(loadDoc(projectFile), projectFile);
    BOOST_REQUIRE(reader->success());
    stages = std::make_unique<StageSequence>(reader->pages(), makeNullAccessor());
    reader->readFilterSettings(stages->filters());
  }

  void checkFixtureUnchanged(const char* name) const {
    const QByteArray now = readBytes(fixturesRoot() + QLatin1Char('/') + QLatin1String(name)
                                     + QStringLiteral("/project.ScanTailor"));
    BOOST_CHECK_MESSAGE(now == frozenBytes, "fixture bytes changed on disk");
  }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(FrozenProjectFixturesTestSuite)

// The frozen image project opens, and every pinned non-default value survives.
BOOST_AUTO_TEST_CASE(frozen_image_project_opens_with_expected_params) {
  OpenedFixture fixture("image-project");

  const PageSequence pageSeq = fixture.reader->pages()->toPageSequence(PAGE_VIEW);
  BOOST_REQUIRE_EQUAL(pageSeq.numPages(), 3u);
  const PageId page1 = pageSeq.pageAt(static_cast<size_t>(0)).id();

  // deskew
  {
    const auto params = fixture.stages->deskewFilter()->settings()->getPageParams(page1);
    BOOST_REQUIRE(params != nullptr);
    BOOST_CHECK_CLOSE_FRACTION(params->deskewAngle(), BuilderExpectations::deskewAnglePage1, 1e-9);
    BOOST_CHECK_EQUAL(params->mode(), MODE_MANUAL);
  }
  // page_box
  {
    const auto params = fixture.stages->pageBoxFilter()->settings()->getPageParams(page1);
    BOOST_REQUIRE(params != nullptr);
    BOOST_CHECK(params->pageRect() == BuilderExpectations::pageBoxRect);
  }
  // select_content
  {
    const auto params = fixture.stages->selectContentFilter()->settings()->getPageParams(page1);
    BOOST_REQUIRE(params != nullptr);
    BOOST_CHECK(params->contentRect() == BuilderExpectations::contentRect);
  }
  // finalize
  {
    const auto params = fixture.stages->finalizeFilter()->settings()->getParams(page1);
    BOOST_REQUIRE(params != nullptr);
    BOOST_CHECK(params->colorMode() == finalize::ColorMode::Mixed);
    BOOST_CHECK(params->isProcessed());
    BOOST_CHECK_EQUAL(params->detectionSensitivity(), 7);
  }
  // output
  {
    const output::Params params = fixture.stages->outputFilter()->settings()->getParams(page1);
    BOOST_CHECK_EQUAL(params.outputDpi().horizontal(), BuilderExpectations::outputDpi);
    BOOST_CHECK_CLOSE_FRACTION(params.despeckleLevel(), BuilderExpectations::outputDespeckleLevel, 1e-9);
    BOOST_CHECK_EQUAL(params.colorParams().colorMode(), output::MIXED);
    BOOST_CHECK_EQUAL(params.dewarpingOptions().dewarpingMode(), output::MANUAL);
    const ZoneSet zones = fixture.stages->outputFilter()->settings()->pictureZonesForPage(page1);
    BOOST_CHECK_EQUAL(std::distance(zones.begin(), zones.end()), 1);
  }
  // ocr
  {
    const auto result = fixture.stages->ocrFilter()->settings()->getOcrResult(page1);
    BOOST_REQUIRE(result != nullptr);
    BOOST_CHECK_EQUAL(result->words().size(), 2);
    BOOST_CHECK(result->language() == BuilderExpectations::ocrLanguage);
  }
  // export
  {
    auto settings = fixture.stages->exportFilter()->settings();
    BOOST_CHECK(settings->bookMetadata().title == BuilderExpectations::exportTitle);
    BOOST_CHECK(settings->noDpiLimit());
    BOOST_CHECK(settings->quality() == PdfExporter::Quality::Low);
  }
  // fix_orientation: no settings accessor; verify through a re-save.
  {
    const QString resaved = writeProjectXml(*fixture.stages, fixture.reader->pages(),
                                            fixture.temp.path() + QStringLiteral("/resave.ScanTailor"),
                                            fixture.temp.path() + QStringLiteral("/out"));
    BOOST_REQUIRE(!resaved.isEmpty());
    BOOST_CHECK(resaved.contains(QStringLiteral("degrees=\"90\"")));
  }

  fixture.checkFixtureUnchanged("image-project");
}

// [F1 legacy] The frozen 600-DPI PDF-import project predates the QW5
// pdfImportDpi attribute: metadata still says 600 DPI, but reopening renders
// at the 300-DPI default because the frozen file carries no import DPI. This
// pins, explicitly and deliberately, the absence path: pre-QW5 projects keep
// the old behavior (and, with it, the F1 corruption trigger — fixing THEM
// retroactively is impossible, the information was never saved).
BOOST_AUTO_TEST_CASE(frozen_pdf600_project_documents_f1_dpi_loss) {
  OpenedFixture fixture("pdf600-project");

  const PageSequence pageSeq = fixture.reader->pages()->toPageSequence(PAGE_VIEW);
  BOOST_REQUIRE_EQUAL(pageSeq.numPages(), 1u);
  const PageInfo pageInfo = pageSeq.pageAt(static_cast<size_t>(0));

  // The stored metadata carries the import-resolution size and DPI.
  BOOST_CHECK_EQUAL(pageInfo.metadata().size().width(), Pdf600Expectations::metadataWidth);
  BOOST_CHECK_EQUAL(pageInfo.metadata().size().height(), Pdf600Expectations::metadataHeight);
  BOOST_CHECK_EQUAL(pageInfo.metadata().dpi().horizontal(), Pdf600Expectations::importDpi);

  const QString pdfPath = pageInfo.imageId().filePath();
  BOOST_REQUIRE(PdfReader::canRead(pdfPath));

  // [F1 legacy] The frozen file has no pdfImportDpi attribute, so nothing on
  // the open path restores the import DPI: the render DPI is the 300 default.
  BOOST_CHECK_EQUAL(PdfReader::getImportDpi(pdfPath), 300);

  // [F1 legacy] The production load path (ImageLoader, exactly what
  // LoadFileTask calls) therefore renders at half the stored size...
  const QImage rendered = ImageLoader::load(pageInfo.imageId());
  BOOST_REQUIRE(!rendered.isNull());
  BOOST_CHECK_EQUAL(rendered.width(), Pdf600Expectations::defaultRenderWidth);
  BOOST_CHECK_EQUAL(rendered.height(), Pdf600Expectations::defaultRenderHeight);

  // ...which is precisely the size mismatch that makes
  // LoadFileTask::updateImageSizeIfChanged silently rewrite the stored
  // metadata (and the next save persist the corruption). Documented here as
  // the trigger condition; the rewrite itself is not driven in this test.
  BOOST_CHECK(rendered.size() != pageInfo.metadata().size());

  // The 600-DPI geometry params read back unscaled (reader/writer preserve
  // them; only the LoadFileTask path corrupts).
  const auto contentParams = fixture.stages->selectContentFilter()->settings()->getPageParams(pageInfo.id());
  BOOST_REQUIRE(contentParams != nullptr);
  BOOST_CHECK(contentParams->contentRect() == Pdf600Expectations::contentRect);
  const auto deskewParams = fixture.stages->deskewFilter()->settings()->getPageParams(pageInfo.id());
  BOOST_REQUIRE(deskewParams != nullptr);
  BOOST_CHECK_CLOSE_FRACTION(deskewParams->deskewAngle(), Pdf600Expectations::deskewAngle, 1e-9);

  fixture.checkFixtureUnchanged("pdf600-project");
}

// [QW5] The fixed flow, driven end-to-end on a temp copy of the fixture:
// once the import DPI is chosen (as the import dialog does), a save persists
// it as the pdfImportDpi attribute on the <file> element, and reopening that
// save restores it into PdfReader's DPI map during ProjectReader parsing —
// before any page render — so the production load path renders at the stored
// metadata size and updateImageSizeIfChanged's rewrite trigger is gone.
// Also covers the new attribute through the writer/reader pair generation-
// over-generation: second- and third-generation saves are byte-identical.
BOOST_AUTO_TEST_CASE(pdf600_import_dpi_roundtrip_restores_600) {
  OpenedFixture fixture("pdf600-project");

  const PageSequence pageSeq = fixture.reader->pages()->toPageSequence(PAGE_VIEW);
  BOOST_REQUIRE_EQUAL(pageSeq.numPages(), 1u);
  const PageInfo pageInfo = pageSeq.pageAt(static_cast<size_t>(0));
  const QString pdfPath = pageInfo.imageId().filePath();

  // Simulate the import-dialog choice this fixture was created with.
  PdfReader::setImportDpi(pdfPath, Pdf600Expectations::importDpi);

  // Save 1: the attribute is now emitted.
  const QString resavePath = fixture.temp.path() + QStringLiteral("/resave.ScanTailor");
  const QString outDir = fixture.temp.path() + QStringLiteral("/out");
  const QString resave1 = writeProjectXml(*fixture.stages, fixture.reader->pages(), resavePath, outDir);
  BOOST_REQUIRE(!resave1.isEmpty());
  BOOST_CHECK(resave1.contains(QStringLiteral("pdfImportDpi=\"600\"")));

  // Simulate a fresh app session where a stale/default DPI is in the map.
  PdfReader::setImportDpi(pdfPath, 300);

  // Reopen the save: parsing must restore the 600-DPI choice.
  QDomDocument doc;
  BOOST_REQUIRE(doc.setContent(resave1));
  const ProjectReader reader2(doc, resavePath);
  BOOST_REQUIRE(reader2.success());
  BOOST_CHECK_EQUAL(PdfReader::getImportDpi(pdfPath), Pdf600Expectations::importDpi);

  // The production load path now renders at the stored metadata size: the
  // updateImageSizeIfChanged corruption trigger (size mismatch) is gone.
  const QImage rendered = ImageLoader::load(pageInfo.imageId());
  BOOST_REQUIRE(!rendered.isNull());
  BOOST_CHECK_EQUAL(rendered.width(), Pdf600Expectations::metadataWidth);
  BOOST_CHECK_EQUAL(rendered.height(), Pdf600Expectations::metadataHeight);
  BOOST_CHECK(rendered.size() == pageInfo.metadata().size());

  // Generation 2 and 3 both include the attribute and are byte-identical
  // (the v4 dialect's save->load->save identity survives the addition).
  StageSequence stages2(reader2.pages(), makeNullAccessor());
  reader2.readFilterSettings(stages2.filters());
  const QString resave2Path = fixture.temp.path() + QStringLiteral("/resave2.ScanTailor");
  const QString resave2 = writeProjectXml(stages2, reader2.pages(), resave2Path, outDir);
  BOOST_REQUIRE(!resave2.isEmpty());
  BOOST_CHECK(resave2.contains(QStringLiteral("pdfImportDpi=\"600\"")));

  QDomDocument doc2;
  BOOST_REQUIRE(doc2.setContent(resave2));
  const ProjectReader reader3(doc2, resave2Path);
  BOOST_REQUIRE(reader3.success());
  StageSequence stages3(reader3.pages(), makeNullAccessor());
  reader3.readFilterSettings(stages3.filters());
  const QString resave3 = writeProjectXml(stages3, reader3.pages(),
                                          fixture.temp.path() + QStringLiteral("/resave3.ScanTailor"), outDir);
  BOOST_CHECK_MESSAGE(resave2 == resave3, "pdfImportDpi save->load->save is not idempotent");

  fixture.checkFixtureUnchanged("pdf600-project");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
