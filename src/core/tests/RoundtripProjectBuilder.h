// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Shared builder for Stage 1.5 persistence characterization: constructs an
// in-memory project with ALL 10 filters populated with non-default per-page
// params. Used by TestProjectSaveLoadSave (save->load->save equality), the
// frozen-fixture writer (project_fixture_writer), and the frozen-fixture tests
// (which assert against the constants defined here).

#ifndef SCANTAILOR_TESTS_ROUNDTRIPPROJECTBUILDER_H_
#define SCANTAILOR_TESTS_ROUNDTRIPPROJECTBUILDER_H_

#include <QDomDocument>
#include <QRectF>
#include <QString>
#include <memory>
#include <vector>

#include "ImageId.h"
#include "PageId.h"
#include "PageSelectionAccessor.h"

class ProjectPages;
class StageSequence;

namespace Tests {

// Known non-default values written by populateAllFilters and asserted by the
// frozen-fixture tests. Change these only together with regenerated fixtures.
struct BuilderExpectations {
  static constexpr double deskewAnglePage1 = 1.25;
  static constexpr double outputDespeckleLevel = 2.5;
  static constexpr int outputDpi = 400;
  static constexpr int fixOrientationDegrees = 90;
  static const QRectF pageBoxRect;       // (12, 18, 940, 1330)
  static const QRectF contentRect;       // (40, 60, 860, 1240)
  static const QString ocrLanguage;      // "de-DE"
  static const QString exportTitle;      // "Characterization Fixtures"
};

// Known values of the frozen 600-DPI PDF-import fixture (the F1 case). The
// PDF page is 144x216 pt (2x3 in): 1200x1800 px at 600 DPI, 600x900 at 300.
struct Pdf600Expectations {
  static constexpr int importDpi = 600;
  static constexpr int metadataWidth = 1200;
  static constexpr int metadataHeight = 1800;
  static constexpr int defaultRenderWidth = 600;   // at PdfReader's 300-DPI default
  static constexpr int defaultRenderHeight = 900;
  static constexpr double deskewAngle = 0.5;
  static const QRectF contentRect;  // (100, 150, 1000, 1500) in 600-DPI px
};

PageSelectionAccessor makeNullAccessor();

struct ProjectModel {
  std::shared_ptr<ProjectPages> pages;
  ImageId imageId1;  // single page
  ImageId imageId2;  // two-page spread
  PageId page1;
  PageId page2Left;
  PageId page2Right;

  explicit ProjectModel(const QString& scansDir);

  std::vector<PageId> allPageIds() const { return {page1, page2Left, page2Right}; }
};

// Populates 9 of the 10 filters with non-default per-page params through their
// public Settings APIs. fix_orientation has no public settings accessor; use
// injectFixOrientationRotation on the serialized document instead.
void populateAllFilters(StageSequence& stages, const ProjectModel& model);

// Populates deskew + select_content for the 600-DPI PDF fixture page with
// geometry expressed in 600-DPI image coordinates (Pdf600Expectations).
void populatePdf600Filters(StageSequence& stages, const PageId& pageId);

// Writes the project; returns the XML text (empty on failure).
QString writeProjectXml(StageSequence& stages,
                        const std::shared_ptr<ProjectPages>& pages,
                        const QString& projectFile,
                        const QString& outDir);

// Injects a non-default fix_orientation rotation using the numeric image id
// the writer assigned in the document's own <images> section. Returns false if
// the document does not have the expected shape.
bool injectFixOrientationRotation(QDomDocument& doc, int degrees);

}  // namespace Tests

#endif  // SCANTAILOR_TESTS_ROUNDTRIPPROJECTBUILDER_H_
