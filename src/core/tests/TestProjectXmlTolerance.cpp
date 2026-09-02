// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Stage 1.5 characterization of ProjectReader/Writer tolerance behavior
// (audit §6.4, prereqs Part 2e). Pins CURRENT behavior — none of this is an
// endorsement:
//  - version wall: version != 4 rejected in both directions (3 and 5);
//  - unknown elements/attributes are silently DROPPED on the next save
//    (ProjectWriter rebuilds the document from memory);
//  - a malformed per-page element is silently skipped on load (page falls back
//    to defaults) and permanently erased by the next save.

#include <ProjectReader.h>
#include <ProjectWriter.h>
#include <StageSequence.h>
#include <version.h>

#include <QDir>
#include <QDomDocument>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <set>
#include <vector>

#include "Dpi.h"
#include "FileNameDisambiguator.h"
#include "ImageId.h"
#include "ImageInfo.h"
#include "ImageMetadata.h"
#include "Margins.h"
#include "OutputFileNameGenerator.h"
#include "PageId.h"
#include "PageSelectionAccessor.h"
#include "PageSelectionProvider.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "SelectedPage.h"
#include "filters/deskew/Filter.h"
#include "filters/deskew/Settings.h"

namespace Tests {
namespace {

class NullPageSelectionProvider : public PageSelectionProvider {
 public:
  PageSequence allPages() const override { return PageSequence(); }
  std::set<PageId> selectedPages() const override { return {}; }
  std::vector<PageRange> selectedRanges() const override { return {}; }
};

PageSelectionAccessor makeAccessor() {
  return PageSelectionAccessor(std::make_shared<NullPageSelectionProvider>());
}

struct Env {
  QTemporaryDir temp;
  QString scansDir;
  QString outDir;
  std::shared_ptr<ProjectPages> pages;
  PageId pageId;

  Env() {
    BOOST_REQUIRE(temp.isValid());
    const QDir dir(temp.path());
    BOOST_REQUIRE(dir.mkpath("scans"));
    BOOST_REQUIRE(dir.mkpath("out"));
    scansDir = dir.filePath("scans");
    outDir = dir.filePath("out");

    const ImageId imageId(scansDir + "/page1.tif", 0);
    const ImageMetadata metadata(QSize(1000, 1400), Dpi(300, 300));
    const std::vector<ImageInfo> images{ImageInfo(imageId, metadata, 1, false, false)};
    pages = std::make_shared<ProjectPages>(images, Qt::LeftToRight);
    pageId = PageId(imageId, PageId::SINGLE_PAGE);
  }

  QString filePath(const char* name) const { return QDir(temp.path()).filePath(name); }

  // Save with a full (populated) filter chain; returns the XML text.
  QString save(StageSequence& stages, const char* name) const {
    const OutputFileNameGenerator gen(std::make_shared<FileNameDisambiguator>(), outDir, Qt::LeftToRight);
    const ProjectWriter writer(pages, SelectedPage(), gen);
    BOOST_REQUIRE(writer.write(filePath(name), stages.filters()));
    QFile file(filePath(name));
    BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
  }
};

QDomDocument parseXml(const QString& text) {
  QDomDocument doc;
  BOOST_REQUIRE(doc.setContent(text));
  return doc;
}

void populateDeskew(StageSequence& stages, const PageId& pageId) {
  const deskew::Dependencies deps(QPolygonF(QRectF(0, 0, 990, 1390)), OrthogonalRotation());
  stages.deskewFilter()->settings()->setPageParams(
      pageId, deskew::Params(3.5, deps, MODE_MANUAL, QStringLiteral("tolerance-test")));
}

}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectXmlToleranceTestSuite)

// The version wall: exact match on PROJECT_VERSION (4); both older and newer
// are rejected. The only compat shim is the per-file multiPage attribute.
BOOST_AUTO_TEST_CASE(version_wall_rejects_both_directions) {
  Env env;
  StageSequence stages(env.pages, makeAccessor());
  const QString xml = env.save(stages, "project.ScanTailor");

  BOOST_REQUIRE_EQUAL(PROJECT_VERSION, 4);

  {
    QDomDocument doc = parseXml(xml);
    BOOST_CHECK_EQUAL(doc.documentElement().attribute("version").toStdString(), "4");
    const ProjectReader reader(doc, env.filePath("project.ScanTailor"));
    BOOST_CHECK(reader.success());
  }
  for (const char* version : {"3", "5"}) {
    QDomDocument doc = parseXml(xml);
    doc.documentElement().setAttribute("version", version);
    const ProjectReader reader(doc, env.filePath("project.ScanTailor"));
    BOOST_CHECK_MESSAGE(!reader.success(), "version " << version << " unexpectedly accepted");
  }
}

// Unknown elements and attributes vanish on the next save: the writer rebuilds
// the whole document from memory with no pass-through.
BOOST_AUTO_TEST_CASE(unknown_xml_dropped_on_next_save) {
  Env env;
  StageSequence stages(env.pages, makeAccessor());
  populateDeskew(stages, env.pageId);
  const QString xml = env.save(stages, "project.ScanTailor");

  QDomDocument doc = parseXml(xml);
  QDomElement root = doc.documentElement();
  // Unknown root-level element, unknown root attribute, and an unknown child
  // inside a known filter section.
  QDomElement mystery = doc.createElement("mystery-extension");
  mystery.setAttribute("payload", "future-format-data");
  root.appendChild(mystery);
  root.setAttribute("futureAttribute", "42");
  QDomElement deskewEl = root.namedItem("filters").namedItem("deskew").toElement();
  BOOST_REQUIRE(!deskewEl.isNull());
  deskewEl.appendChild(doc.createElement("unknown-deskew-child"));

  const ProjectReader reader(doc, env.filePath("project.ScanTailor"));
  BOOST_REQUIRE(reader.success());
  StageSequence stages2(reader.pages(), makeAccessor());
  reader.readFilterSettings(stages2.filters());

  const OutputFileNameGenerator gen(std::make_shared<FileNameDisambiguator>(), env.outDir, Qt::LeftToRight);
  const ProjectWriter writer(reader.pages(), SelectedPage(), gen);
  BOOST_REQUIRE(writer.write(env.filePath("resave.ScanTailor"), stages2.filters()));
  QFile file(env.filePath("resave.ScanTailor"));
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  const QString resaved = QString::fromUtf8(file.readAll());

  BOOST_CHECK(!resaved.contains(QStringLiteral("mystery-extension")));
  BOOST_CHECK(!resaved.contains(QStringLiteral("futureAttribute")));
  BOOST_CHECK(!resaved.contains(QStringLiteral("unknown-deskew-child")));
  // The known data is still there.
  BOOST_CHECK(resaved.contains(QStringLiteral("deskew")));
  BOOST_CHECK(resaved.contains(QStringLiteral("tolerance-test")));
}

// Malformed per-page element: load silently skips it (open succeeds, page on
// defaults) and the next save permanently erases the original element.
BOOST_AUTO_TEST_CASE(malformed_page_element_silently_dropped) {
  Env env;
  StageSequence stages(env.pages, makeAccessor());
  populateDeskew(stages, env.pageId);
  const QString xml = env.save(stages, "project.ScanTailor");
  BOOST_REQUIRE(xml.contains(QStringLiteral("tolerance-test")));

  QDomDocument doc = parseXml(xml);
  QDomElement pageEl
      = doc.documentElement().namedItem("filters").namedItem("deskew").namedItem("page").toElement();
  BOOST_REQUIRE(!pageEl.isNull());
  pageEl.setAttribute("id", "not-a-number");  // unparsable id -> `continue`

  const ProjectReader reader(doc, env.filePath("project.ScanTailor"));
  BOOST_REQUIRE(reader.success());  // per-page tolerance: open still succeeds
  StageSequence stages2(reader.pages(), makeAccessor());
  reader.readFilterSettings(stages2.filters());

  // The page fell back to defaults: no stored deskew params.
  BOOST_CHECK(stages2.deskewFilter()->settings()->getPageParams(env.pageId) == nullptr);

  const OutputFileNameGenerator gen(std::make_shared<FileNameDisambiguator>(), env.outDir, Qt::LeftToRight);
  const ProjectWriter writer(reader.pages(), SelectedPage(), gen);
  BOOST_REQUIRE(writer.write(env.filePath("resave.ScanTailor"), stages2.filters()));
  QFile file(env.filePath("resave.ScanTailor"));
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  const QString resaved = QString::fromUtf8(file.readAll());

  // The malformed-but-possibly-recoverable original data is gone for good.
  BOOST_CHECK(!resaved.contains(QStringLiteral("tolerance-test")));
  BOOST_CHECK(!resaved.contains(QStringLiteral("not-a-number")));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
