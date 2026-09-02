// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputGoldenHarness.h"

#include <BinaryImage.h>
#include <NullTaskStatus.h>

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdlib>
#include <memory>
#include <vector>

#include "Dpi.h"
#include "Dpm.h"
#include "FilterData.h"
#include "ImageId.h"
#include "PageId.h"
#include "dewarping/Curve.h"
#include "dewarping/DistortionModel.h"
#include "filters/output/DepthPerception.h"
#include "filters/output/DewarpingOptions.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"
#include "zones/SerializableSpline.h"
#include "zones/Zone.h"
#include "zones/ZoneSet.h"

namespace Tests {
namespace {

// Content rectangle inside the 600x800 page (margins all around).
const QRectF kContentRect(60, 60, 480, 680);

// Fixed MANUAL distortion model: mildly bowed top/bottom curves whose
// endpoints form a convex quadrilateral. Committed constants — never derived
// from content, so the dewarp driver runs deterministically.
dewarping::DistortionModel makeFixedDistortionModel() {
  const std::vector<QPointF> top{{40, 50}, {170, 42}, {300, 40}, {430, 42}, {560, 50}};
  const std::vector<QPointF> bottom{{40, 750}, {170, 758}, {300, 760}, {430, 758}, {560, 750}};
  dewarping::DistortionModel model;
  model.setTopCurve(dewarping::Curve(top));
  model.setBottomCurve(dewarping::Curve(bottom));
  return model;
}

output::Params makeParams(const GoldenCase& goldenCase) {
  output::Params params;
  params.setOutputDpi(Dpi(300, 300));

  output::ColorParams colorParams = params.colorParams();
  colorParams.setColorMode(goldenCase.colorMode);
  colorParams.setColorModeUserSet(true);

  output::ColorCommonOptions commonOptions = colorParams.colorCommonOptions();
  commonOptions.setFillingColor(goldenCase.fillingColor);
  commonOptions.setFillMargins(true);
  commonOptions.setWienerCoef(goldenCase.wienerCoef);
  colorParams.setColorCommonOptions(commonOptions);
  params.setColorParams(colorParams);

  output::SplittingOptions splittingOptions;
  splittingOptions.setSplitOutput(goldenCase.splitOutput);
  splittingOptions.setSplittingMode(goldenCase.splittingMode);
  splittingOptions.setOriginalBackgroundEnabled(goldenCase.originalBackground);
  params.setSplittingOptions(splittingOptions);

  output::PictureShapeOptions pictureShapeOptions;
  pictureShapeOptions.setPictureShape(goldenCase.pictureShape);
  params.setPictureShapeOptions(pictureShapeOptions);

  if (goldenCase.dewarp) {
    params.setDewarpingOptions(output::DewarpingOptions(output::MANUAL, /*needPostDeskew=*/false));
    params.setDistortionModel(makeFixedDistortionModel());
  }
  return params;
}

}  // namespace

GoldenRender renderGoldenCase(const GoldenCase& goldenCase, const QImage& source) {
  QImage src = source;
  src.setDotsPerMeterX(Dpm(Dpi(300, 300)).horizontal());
  src.setDotsPerMeterY(Dpm(Dpi(300, 300)).vertical());

  const FilterData data(src);
  const PageId pageId(ImageId(QStringLiteral("golden://%1").arg(QLatin1String(goldenCase.name)), 0));

  auto settings = std::make_shared<output::Settings>();
  settings->setParams(pageId, makeParams(goldenCase));

  const QPolygonF contentPolygon{kContentRect};
  const output::OutputGenerator generator(data.xform(), contentPolygon);

  ZoneSet pictureZones;
  ZoneSet fillZones;
  if (goldenCase.addFillZone) {
    // Fill zone over part of the text block; default properties (white fill).
    fillZones.add(Zone(SerializableSpline(QPolygonF(QRectF(80, 100, 200, 120)))));
  }

  dewarping::DistortionModel distortionModel;
  if (goldenCase.dewarp) {
    distortionModel = makeFixedDistortionModel();
  }

  imageproc::BinaryImage automask;
  const bool wantAutomask
      = (goldenCase.colorMode == output::MIXED) && (goldenCase.pictureShape != output::OFF_SHAPE);

  NullTaskStatus status;
  std::unique_ptr<output::OutputImage> image = generator.process(
      status, data, pictureZones, fillZones, distortionModel, output::DepthPerception(),
      wantAutomask ? &automask : nullptr, nullptr, nullptr, pageId, settings);

  GoldenRender render;
  render.image = image->toImage();
  if (wantAutomask && !automask.isNull()) {
    render.automask = automask.toQImage();
  }
  return render;
}

QString goldenFixtureDir() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/golden");
}

bool goldenRegenRequested() {
  const char* env = std::getenv("ST_GOLDEN_REGEN");
  return env != nullptr && env[0] != '\0';
}

bool compareOrRegenGolden(const QString& name, const QImage& rendered, QString* failureDetail) {
  const QString goldenPath = goldenFixtureDir() + QStringLiteral("/goldens/") + name + QStringLiteral(".png");
  const QString metaPath = goldenFixtureDir() + QStringLiteral("/goldens/") + name + QStringLiteral(".meta");

  if (goldenRegenRequested()) {
    QDir().mkpath(goldenFixtureDir() + QStringLiteral("/goldens"));
    if (!rendered.save(goldenPath, "PNG")) {
      if (failureDetail) *failureDetail = QStringLiteral("failed to write golden ") + goldenPath;
      return false;
    }
    QFile meta(metaPath);
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      if (failureDetail) *failureDetail = QStringLiteral("failed to write meta ") + metaPath;
      return false;
    }
    QTextStream stream(&meta);
    stream << "format=" << static_cast<int>(rendered.format()) << "\nwidth=" << rendered.width()
           << "\nheight=" << rendered.height() << "\n";
    return true;
  }

  const QImage golden(goldenPath);
  if (golden.isNull()) {
    if (failureDetail) *failureDetail = QStringLiteral("missing golden ") + goldenPath;
    return false;
  }

  // Pinned format from the freeze-time render (PNG round-trips can change the
  // in-memory format, so the .meta file is authoritative for the format pin).
  QFile meta(metaPath);
  if (!meta.open(QIODevice::ReadOnly)) {
    if (failureDetail) *failureDetail = QStringLiteral("missing meta ") + metaPath;
    return false;
  }
  const QString metaText = QString::fromUtf8(meta.readAll());
  const QString expectedFormatLine = QStringLiteral("format=%1").arg(static_cast<int>(rendered.format()));
  if (!metaText.contains(expectedFormatLine)) {
    if (failureDetail) {
      *failureDetail = QStringLiteral("%1: rendered format %2 does not match frozen %3")
                           .arg(name, QString::number(static_cast<int>(rendered.format())),
                                metaText.simplified());
    }
    return false;
  }

  if (golden.size() != rendered.size()) {
    if (failureDetail) {
      *failureDetail = QStringLiteral("%1: size %2x%3 vs golden %4x%5")
                           .arg(name)
                           .arg(rendered.width())
                           .arg(rendered.height())
                           .arg(golden.width())
                           .arg(golden.height());
    }
    return false;
  }

  const QImage a = rendered.convertToFormat(QImage::Format_ARGB32);
  const QImage b = golden.convertToFormat(QImage::Format_ARGB32);
  qint64 diffCount = 0;
  int firstX = -1, firstY = -1;
  for (int y = 0; y < a.height(); ++y) {
    const QRgb* lineA = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const QRgb* lineB = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      if (lineA[x] != lineB[x]) {
        if (diffCount == 0) {
          firstX = x;
          firstY = y;
        }
        ++diffCount;
      }
    }
  }
  if (diffCount > 0) {
    rendered.save(goldenFixtureDir() + QStringLiteral("/goldens/") + name + QStringLiteral(".actual.png"), "PNG");
    if (failureDetail) {
      *failureDetail = QStringLiteral("%1: %2 differing pixels (first at %3,%4); wrote %1.actual.png")
                           .arg(name)
                           .arg(diffCount)
                           .arg(firstX)
                           .arg(firstY);
    }
    return false;
  }
  return true;
}

}  // namespace Tests
