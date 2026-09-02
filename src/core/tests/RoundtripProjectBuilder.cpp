// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "RoundtripProjectBuilder.h"

#include <ProjectWriter.h>
#include <StageSequence.h>

#include <QFile>
#include <QLineF>
#include <QPolygonF>
#include <set>

#include "BookMetadata.h"
#include "Dpi.h"
#include "FileNameDisambiguator.h"
#include "ImageInfo.h"
#include "ImageMetadata.h"
#include "Margins.h"
#include "OutputFileNameGenerator.h"
#include "PageSelectionProvider.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "SelectedPage.h"
#include "filters/deskew/Filter.h"
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
#include "filters/page_box/Settings.h"
#include "filters/page_layout/Alignment.h"
#include "filters/page_layout/Filter.h"
#include "filters/page_layout/Params.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_split/Filter.h"
#include "filters/page_split/Settings.h"
#include "filters/select_content/Filter.h"
#include "filters/select_content/Settings.h"
#include "zones/SerializableSpline.h"
#include "zones/Zone.h"
#include "zones/ZoneSet.h"

namespace Tests {
namespace {

class NullPageSelectionProvider : public PageSelectionProvider {
 public:
  PageSequence allPages() const override { return PageSequence(); }
  std::set<PageId> selectedPages() const override { return {}; }
  std::vector<PageRange> selectedRanges() const override { return {}; }
};

}  // namespace

const QRectF BuilderExpectations::pageBoxRect(12, 18, 940, 1330);
const QRectF Pdf600Expectations::contentRect(100, 150, 1000, 1500);
const QRectF BuilderExpectations::contentRect(40, 60, 860, 1240);
const QString BuilderExpectations::ocrLanguage = QStringLiteral("de-DE");
const QString BuilderExpectations::exportTitle = QStringLiteral("Characterization Fixtures");

PageSelectionAccessor makeNullAccessor() {
  return PageSelectionAccessor(std::make_shared<NullPageSelectionProvider>());
}

ProjectModel::ProjectModel(const QString& scansDir) {
  imageId1 = ImageId(scansDir + "/page1.png", 0);
  imageId2 = ImageId(scansDir + "/spread.png", 0);
  const ImageMetadata meta1(QSize(1000, 1400), Dpi(300, 300));
  const ImageMetadata meta2(QSize(2000, 1400), Dpi(300, 300));
  const std::vector<ImageInfo> images{
      ImageInfo(imageId1, meta1, 1, false, false),
      ImageInfo(imageId2, meta2, 2, false, false),
  };
  pages = std::make_shared<ProjectPages>(images, Qt::LeftToRight);
  page1 = PageId(imageId1, PageId::SINGLE_PAGE);
  page2Left = PageId(imageId2, PageId::LEFT_PAGE);
  page2Right = PageId(imageId2, PageId::RIGHT_PAGE);
}

void populateAllFilters(StageSequence& stages, const ProjectModel& model) {
  // page_split: manual uncut layout for image1, manual split for image2.
  {
    auto settings = stages.pageSplitFilter()->settings();
    page_split::Settings::UpdateAction uncut;
    uncut.setLayoutType(page_split::SINGLE_PAGE_UNCUT);
    const QRectF fullRect1(0, 0, 1000, 1400);
    uncut.setParams(page_split::Params(
        page_split::PageLayout(fullRect1),
        page_split::Dependencies(QSize(1000, 1400), OrthogonalRotation(), page_split::SINGLE_PAGE_UNCUT),
        MODE_MANUAL, QStringLiteral("characterization")));
    settings->updatePage(model.imageId1, uncut);

    page_split::Settings::UpdateAction split;
    split.setLayoutType(page_split::TWO_PAGES);
    const QRectF fullRect2(0, 0, 2000, 1400);
    split.setParams(page_split::Params(
        page_split::PageLayout(fullRect2, QLineF(QPointF(1000, 0), QPointF(1010, 1400))),
        page_split::Dependencies(QSize(2000, 1400), OrthogonalRotation(), page_split::TWO_PAGES),
        MODE_MANUAL, QStringLiteral("characterization")));
    settings->updatePage(model.imageId2, split);
  }

  // deskew: distinct manual angles per page (page1 pinned by expectations).
  {
    auto settings = stages.deskewFilter()->settings();
    double angle = BuilderExpectations::deskewAnglePage1;
    for (const PageId& pageId : model.allPageIds()) {
      const deskew::Dependencies deps(QPolygonF(QRectF(0, 0, 990, 1390)), OrthogonalRotation());
      settings->setPageParams(pageId, deskew::Params(angle, deps, MODE_MANUAL, QStringLiteral("unit-test")));
      angle += 0.5;
    }
  }

  // page_box: manual page rect with fine-tuned corners.
  {
    auto settings = stages.pageBoxFilter()->settings();
    for (const PageId& pageId : model.allPageIds()) {
      const page_box::Dependencies deps(QPolygonF(QRectF(0, 0, 990, 1390)), MODE_MANUAL, true);
      settings->setPageParams(pageId,
                              page_box::Params(BuilderExpectations::pageBoxRect, deps, MODE_MANUAL, true));
    }
  }

  // select_content: manual content boxes.
  {
    auto settings = stages.selectContentFilter()->settings();
    for (const PageId& pageId : model.allPageIds()) {
      const select_content::Dependencies deps(QPolygonF(QRectF(0, 0, 990, 1390)), MODE_MANUAL, MODE_MANUAL, false);
      settings->setPageParams(
          pageId, select_content::Params(BuilderExpectations::contentRect, QSizeF(72.8, 105.0),
                                         BuilderExpectations::pageBoxRect, deps, MODE_MANUAL, MODE_MANUAL, false));
    }
  }

  // page_layout: non-default margins and alignment.
  {
    auto settings = stages.pageLayoutFilter()->settings();
    for (const PageId& pageId : model.allPageIds()) {
      settings->setPageParams(
          pageId, page_layout::Params(Margins(11.5, 7.0, 11.5, 9.0), QRectF(0, 0, 940, 1330),
                                      BuilderExpectations::contentRect, QSizeF(72.8, 105.0),
                                      page_layout::Alignment(page_layout::Alignment::TOP, page_layout::Alignment::LEFT),
                                      false, true));
    }
  }

  // finalize: per-page color modes.
  {
    auto settings = stages.finalizeFilter()->settings();
    finalize::Params params;
    params.setColorMode(finalize::ColorMode::Mixed);
    params.setColorModeDetected(true);
    params.setAutomaticDetection(true);
    params.setDetectorSchemaVersion(3);
    params.setDetectionSensitivity(7);
    params.setProcessed(true);
    params.setForceWhiteBalance(true);
    settings->setParams(model.page1, params);
    finalize::Params grayParams;
    grayParams.setColorMode(finalize::ColorMode::Grayscale);
    grayParams.setProcessed(true);
    settings->setParams(model.page2Left, grayParams);
  }

  // output: non-default dpi/mode/despeckle plus a manual picture zone.
  {
    auto settings = stages.outputFilter()->settings();
    output::Params params;
    params.setOutputDpi(Dpi(BuilderExpectations::outputDpi, BuilderExpectations::outputDpi));
    params.setDespeckleLevel(BuilderExpectations::outputDespeckleLevel);
    output::ColorParams colorParams = params.colorParams();
    colorParams.setColorMode(output::MIXED);
    colorParams.setColorModeUserSet(true);
    params.setColorParams(colorParams);
    output::DewarpingOptions dewarpingOptions(output::MANUAL, false);
    dewarpingOptions.setPostDeskewAngle(0.75);
    params.setDewarpingOptions(dewarpingOptions);
    settings->setParams(model.page1, params);

    ZoneSet zones;
    zones.add(Zone(SerializableSpline(QPolygonF(QRectF(100, 120, 300, 200)))));
    settings->setPictureZones(model.page1, zones);
  }

  // ocr: global settings + one per-page result.
  {
    auto settings = stages.ocrFilter()->settings();
    settings->setOcrEnabled(true);
    settings->setLanguage(BuilderExpectations::ocrLanguage);
    settings->setUseAccurateRecognition(true);
    settings->setUseLanguageCorrection(false);
    ocr::OcrResult result;
    result.setImageDimensions(940, 1330);
    result.setLanguage(BuilderExpectations::ocrLanguage);
    result.setAccurateRecognition(true);
    result.addWord(ocr::OcrWord(QStringLiteral("Beispiel"), QRectF(40, 60, 120, 24), 0.93f));
    result.addWord(ocr::OcrWord(QStringLiteral("Seite"), QRectF(170, 60, 80, 24), 0.88f));
    settings->setOcrResult(model.page1, result);
  }

  // export: non-default globals + book metadata.
  {
    auto settings = stages.exportFilter()->settings();
    settings->setNoDpiLimit(true);
    settings->setMaxDpi(500);
    settings->setCompressGrayscale(true);
    settings->setQuality(PdfExporter::Quality::Low);
    BookMetadata meta;
    meta.title = BuilderExpectations::exportTitle;
    meta.authors = QStringLiteral("Ada Example");
    meta.year = QStringLiteral("2026");
    meta.publisher = QStringLiteral("Test Press");
    meta.place = QStringLiteral("Newark");
    meta.isbn = QStringLiteral("9780000000000");
    meta.language = QStringLiteral("en");
    meta.creatorRole = CreatorRole::Editor;
    settings->setBookMetadata(meta);
    settings->setSendToZotero(true, true);
  }
}

void populatePdf600Filters(StageSequence& stages, const PageId& pageId) {
  const QRectF pageRect(0, 0, Pdf600Expectations::metadataWidth, Pdf600Expectations::metadataHeight);
  {
    const deskew::Dependencies deps{QPolygonF(pageRect), OrthogonalRotation()};
    stages.deskewFilter()->settings()->setPageParams(
        pageId, deskew::Params(Pdf600Expectations::deskewAngle, deps, MODE_MANUAL, QStringLiteral("pdf600-fixture")));
  }
  {
    const select_content::Dependencies deps{QPolygonF(pageRect), MODE_MANUAL, MODE_MANUAL, false};
    stages.selectContentFilter()->settings()->setPageParams(
        pageId, select_content::Params(Pdf600Expectations::contentRect, QSizeF(42.3, 63.5), pageRect, deps,
                                       MODE_MANUAL, MODE_MANUAL, false));
  }
}

QString writeProjectXml(StageSequence& stages,
                        const std::shared_ptr<ProjectPages>& pages,
                        const QString& projectFile,
                        const QString& outDir) {
  const OutputFileNameGenerator gen(std::make_shared<FileNameDisambiguator>(), outDir, Qt::LeftToRight);
  const ProjectWriter writer(pages, SelectedPage(), gen);
  if (!writer.write(projectFile, stages.filters())) {
    return QString();
  }
  QFile file(projectFile);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }
  return QString::fromUtf8(file.readAll());
}

bool injectFixOrientationRotation(QDomDocument& doc, int degrees) {
  const QDomElement firstImage = doc.documentElement().namedItem("images").firstChildElement("image");
  if (firstImage.isNull() || !firstImage.hasAttribute("id")) {
    return false;
  }
  QDomElement filterEl = doc.documentElement().namedItem("filters").namedItem("fix-orientation").toElement();
  if (filterEl.isNull()) {
    return false;
  }
  QDomElement imageEl(doc.createElement("image"));
  imageEl.setAttribute("id", firstImage.attribute("id"));
  QDomElement rotationEl(doc.createElement("rotation"));
  rotationEl.setAttribute("degrees", degrees);
  imageEl.appendChild(rotationEl);
  filterEl.insertBefore(imageEl, filterEl.firstChild());
  return true;
}

}  // namespace Tests
