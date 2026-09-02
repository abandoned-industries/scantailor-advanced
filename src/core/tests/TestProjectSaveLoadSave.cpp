// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Stage 1.5.2 characterization: project save -> load -> save XML equality with
// ALL 10 filters populated with non-default per-page params.
//
// Closes the audit's biggest untested persistence surface (audit §3.3): the 10
// filter XML dialects. TestProjectPortability writes an EMPTY filter vector;
// this test writes the full StageSequence.
//
// fix_orientation has no public settings accessor, so its non-default value is
// injected into the first serialization at the DOM level (dialect from
// fix_orientation::Filter::writeParams) and then travels load -> save like
// every other filter's data. The builder lives in RoundtripProjectBuilder.cpp,
// shared with the frozen-fixture writer.

#include <ProjectReader.h>
#include <StageSequence.h>

#include <QDir>
#include <QDomDocument>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#include "RoundtripProjectBuilder.h"

namespace Tests {
namespace {

QDomDocument parseXml(const QString& text) {
  QDomDocument doc;
  BOOST_REQUIRE(doc.setContent(text));
  return doc;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectSaveLoadSaveTestSuite)

// save1 (all 10 filters populated) -> load -> save2: byte-identical XML text.
BOOST_AUTO_TEST_CASE(save_load_save_is_identity) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QDir dir(temp.path());
  BOOST_REQUIRE(dir.mkpath("scans"));
  BOOST_REQUIRE(dir.mkpath("out"));
  const QString scansDir = dir.filePath("scans");
  const QString outDir = dir.filePath("out");

  // Phase 1: build in memory, populate, save0.
  ProjectModel model(scansDir);
  StageSequence stages(model.pages, makeNullAccessor());
  populateAllFilters(stages, model);
  const QString save0 = writeProjectXml(stages, model.pages, dir.filePath("save0.ScanTailor"), outDir);
  BOOST_REQUIRE(!save0.isEmpty());

  // Phase 2: inject the fix_orientation rotation (no settings accessor exists)
  // and load the result into a fresh filter chain; this makes save1.
  QDomDocument doc0 = parseXml(save0);
  BOOST_REQUIRE(injectFixOrientationRotation(doc0, BuilderExpectations::fixOrientationDegrees));

  const ProjectReader reader1(doc0, dir.filePath("save0.ScanTailor"));
  BOOST_REQUIRE(reader1.success());
  StageSequence stages1(reader1.pages(), makeNullAccessor());
  reader1.readFilterSettings(stages1.filters());
  const QString save1 = writeProjectXml(stages1, reader1.pages(), dir.filePath("save1.ScanTailor"), outDir);
  BOOST_REQUIRE(!save1.isEmpty());

  // The injected rotation must have survived into save1 (proves the injection
  // reached fix_orientation's settings rather than being dropped).
  BOOST_REQUIRE(save1.contains(QStringLiteral("degrees=\"90\"")));

  // Phase 3: load save1, save again -> save2 must be byte-identical.
  const ProjectReader reader2(parseXml(save1), dir.filePath("save1.ScanTailor"));
  BOOST_REQUIRE(reader2.success());
  StageSequence stages2(reader2.pages(), makeNullAccessor());
  reader2.readFilterSettings(stages2.filters());
  const QString save2 = writeProjectXml(stages2, reader2.pages(), dir.filePath("save2.ScanTailor"), outDir);
  BOOST_REQUIRE(!save2.isEmpty());

  BOOST_CHECK_MESSAGE(save1 == save2, "save->load->save is not idempotent");
  if (save1 != save2) {
    const QStringList lines1 = save1.split('\n');
    const QStringList lines2 = save2.split('\n');
    for (int i = 0; i < std::min(lines1.size(), lines2.size()); ++i) {
      if (lines1[i] != lines2[i]) {
        BOOST_TEST_MESSAGE("first divergence at line " << (i + 1));
        BOOST_TEST_MESSAGE("save1: " << lines1[i].toStdString());
        BOOST_TEST_MESSAGE("save2: " << lines2[i].toStdString());
        break;
      }
    }
  }

  // Spot-check that the non-default values actually serialized: every filter
  // section must be present and non-empty.
  const QDomDocument doc1 = parseXml(save1);
  const QDomElement filtersEl = doc1.documentElement().namedItem("filters").toElement();
  BOOST_REQUIRE(!filtersEl.isNull());
  const char* sections[] = {"fix-orientation", "page-split", "deskew",         "page-box", "select-content",
                            "page-layout",     "finalize",   "output",         "ocr",      "export"};
  for (const char* name : sections) {
    const QDomElement el = filtersEl.namedItem(QLatin1String(name)).toElement();
    BOOST_CHECK_MESSAGE(!el.isNull(), "missing filter section: " << name);
    BOOST_CHECK_MESSAGE(el.hasChildNodes() || el.hasAttributes(), "empty filter section: " << name);
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
