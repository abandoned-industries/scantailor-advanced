// Copyright (C) 2026  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Tests for Finalize color detection on aged/toned paper: the margin-less
// fallback background estimate (WhiteBalance::estimateBackgroundColor /
// detectPaperColor) and cast-compensated classification
// (LeptonicaDetector::detectWithCastCompensation).

#include <LeptonicaDetector.h>
#include <TaskStatus.h>
#include <WhiteBalance.h>

#include <QImage>
#include <QFileInfo>
#include <QPainter>
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <stdexcept>

namespace Tests {

namespace {

const int kPageW = 800;
const int kPageH = 1000;

// Typical aged 1920s book stock: warm tan, strong enough cast that
// pixColorFraction's diffthresh (50) counts it as "color".
const QColor kTanPaper(214, 192, 150);

class CancellingStatus final : public TaskStatus {
 public:
  explicit CancellingStatus(const int checksBeforeCancel)
      : m_checksBeforeCancel(checksBeforeCancel) {}

  void cancel() override { m_cancelled = true; }
  bool isCancelled() const override { return m_cancelled; }
  void throwIfCancelled() const override {
    if (m_cancelled || m_checks++ >= m_checksBeforeCancel) {
      throw std::runtime_error("cancelled");
    }
  }

 private:
  int m_checksBeforeCancel;
  mutable int m_checks = 0;
  bool m_cancelled = false;
};

// Draw simulated text lines: black word-blocks on the given background.
// Produces >10% dark pixels and >30% light pixels (bimodal), no midtones.
QImage makeTextPage(const QColor& paper) {
  QImage image(kPageW, kPageH, QImage::Format_RGB32);
  image.fill(paper);

  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  for (int y = 60; y < kPageH - 60; y += 34) {
    for (int x = 60; x < kPageW - 60; x += 90) {
      painter.drawRect(x, y, 70, 14);  // a "word"
    }
  }
  painter.end();
  return image;
}

// A genuine color photograph (varied saturated hues) occupying the center of
// a tan text page. Hues are kept below paper brightness so the background
// estimate still lands on the paper.
QImage makeColorPhotoOnTanPage() {
  QImage image = makeTextPage(kTanPaper);

  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  const QColor hues[] = {QColor(190, 40, 40),  QColor(40, 150, 60),  QColor(50, 70, 190),
                         QColor(180, 120, 30), QColor(140, 40, 160), QColor(30, 150, 150)};
  const int photoX = 150, photoY = 250, photoW = 500, photoH = 500;
  const int stripeW = photoW / 6;
  for (int i = 0; i < 6; ++i) {
    painter.setBrush(hues[i]);
    painter.drawRect(photoX + i * stripeW, photoY, stripeW, photoH);
  }
  painter.end();
  return image;
}

// A neutral continuous-tone photograph on an otherwise white text page.
// The patch deliberately has smooth luminance changes rather than flat
// synthetic stripes, exercising the same normalized midtone safeguard used
// for real halftones and photographs.
QImage makeGrayscalePhotoOnTextPage() {
  QImage image = makeTextPage(QColor(250, 250, 250));
  const QRect photoRect(150, 250, 500, 500);
  for (int y = photoRect.top(); y <= photoRect.bottom(); ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = photoRect.left(); x <= photoRect.right(); ++x) {
      const int localX = x - photoRect.left();
      const int localY = y - photoRect.top();
      const int ramp = 35 + (localX * 100) / photoRect.width();
      const int texture = ((localX / 18 + localY / 14) % 2) * 12;
      const int gray = ramp + texture;
      line[x] = qRgb(gray, gray, gray);
    }
  }
  return image;
}

QImage makeFullPageGrayscalePhoto() {
  QImage image(kPageW, kPageH, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      // Broad smooth structure plus modest texture survives both background
      // normalization and blur, unlike single-pixel pseudo-random line art.
      const int ramp = 35 + (x * 125) / image.width();
      const int structure = ((x / 48 + y / 42) % 2) * 45;
      const int texture = ((x / 13 + y / 17) % 2) * 12;
      const int gray = std::min(230, ramp + structure + texture);
      line[x] = qRgb(gray, gray, gray);
    }
  }
  return image;
}

QImage makeFullPageColorPhoto() {
  QImage image(kPageW, kPageH, QImage::Format_RGB32);
  const QColor colors[] = {
      QColor(170, 45, 40), QColor(40, 145, 70), QColor(45, 70, 175), QColor(165, 115, 35)};
  for (int y = 0; y < image.height(); ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      line[x] = colors[(x / 80 + y / 100) % 4].rgb();
    }
  }
  return image;
}

QImage makeSmallGrayscaleInset(bool atEdge) {
  QImage image = makeTextPage(QColor(250, 250, 250));
  const QRect inset(atEdge ? 0 : 350, atEdge ? 350 : 450, 90, 90);
  for (int y = inset.top(); y <= inset.bottom(); ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = inset.left(); x <= inset.right(); ++x) {
      const int gray = 80 + ((x + y) % 90);
      line[x] = qRgb(gray, gray, gray);
    }
  }
  return image;
}

QImage makeTextPageWithSparseGrayNoise() {
  QImage image = makeTextPage(QColor(250, 250, 250));
  for (int y = 25; y < image.height(); y += 73) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 25; x < image.width(); x += 79) {
      line[x] = qRgb(125, 125, 125);
    }
  }
  return image;
}

QImage makeLineArtPage(const int style) {
  QImage image = makeTextPage(QColor(250, 250, 250));
  QPainter painter(&image);
  painter.setPen(QPen(Qt::black, 2));
  const QRect art(120, 230, 560, 500);
  if (style == 0) {
    for (int offset = -art.height(); offset < art.width(); offset += 12) {
      painter.drawLine(art.left() + offset, art.top(),
                       art.left() + offset + art.height(), art.bottom());
      painter.drawLine(art.left() + offset, art.bottom(),
                       art.left() + offset + art.height(), art.top());
    }
  } else if (style == 1) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = art.top(); y < art.bottom(); y += 11) {
      for (int x = art.left(); x < art.right(); x += 13) {
        if (((x * 17 + y * 31) % 7) < 4) painter.drawEllipse(x, y, 3, 3);
      }
    }
  } else {
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = art.top(); y < art.bottom(); y += 9) {
      for (int x = art.left(); x < art.right(); x += 9) {
        painter.drawEllipse(x, y, 4, 4);
      }
    }
  }
  painter.end();
  return image;
}

QImage makeIntegratedHalftonePage() {
  QImage image = makeTextPage(QColor(250, 250, 250));
  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  const QRect art(120, 230, 560, 500);
  for (int y = art.top(); y < art.bottom(); y += 6) {
    for (int x = art.left(); x < art.right(); x += 6) {
      const int local = x - art.left();
      const int macroTexture = ((x / 54 + y / 66) % 2);
      const int radius = std::min(3, 1 + (local * 2) / art.width() + macroTexture);
      painter.setBrush(QColor(25 + ((x + y) % 40),
                              25 + ((x + y) % 40),
                              25 + ((x + y) % 40)));
      painter.drawEllipse(x, y, radius * 2, radius * 2);
    }
  }
  painter.end();
  return image;
}

int maxChannelDiff(const QColor& a, const QColor& b) {
  int d = std::abs(a.red() - b.red());
  d = std::max(d, std::abs(a.green() - b.green()));
  d = std::max(d, std::abs(a.blue() - b.blue()));
  return d;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(ColorDetectionTestSuite)

// A content box that hugs the page leaves no margins; the fallback must still
// produce a paper-color estimate close to the actual paper tint.
BOOST_AUTO_TEST_CASE(paper_color_fallback_without_margins) {
  const QImage page = makeTextPage(kTanPaper);
  const QRect hugging = page.rect();  // content box == page: zero margins

  const QColor paper = WhiteBalance::detectPaperColor(page, hugging);
  BOOST_REQUIRE(paper.isValid());
  BOOST_CHECK_LE(maxChannelDiff(paper, kTanPaper), 8);
  BOOST_CHECK(WhiteBalance::hasSignificantCast(paper));
}

// The background estimate on a neutral white page is near-white and carries
// no cast, so no neutralization would be triggered.
BOOST_AUTO_TEST_CASE(background_estimate_neutral_page) {
  const QImage page = makeTextPage(QColor(250, 250, 250));

  const QColor bg = WhiteBalance::estimateBackgroundColor(page);
  BOOST_REQUIRE(bg.isValid());
  BOOST_CHECK_LE(maxChannelDiff(bg, QColor(250, 250, 250)), 6);
  BOOST_CHECK(!WhiteBalance::hasSignificantCast(bg));
}

// A mostly dark page has no paper-like background; the estimate must refuse
// rather than invent one from photo midtones.
BOOST_AUTO_TEST_CASE(background_estimate_rejects_dark_page) {
  QImage dark(kPageW, kPageH, QImage::Format_RGB32);
  dark.fill(QColor(40, 35, 30));

  const QColor bg = WhiteBalance::estimateBackgroundColor(dark);
  BOOST_CHECK(!bg.isValid());
}

// Uniform tan paper with black text: plain detection reads the tint as COLOR,
// cast compensation must classify it as B&W.
BOOST_AUTO_TEST_CASE(tan_text_page_is_not_color) {
  const QImage page = makeTextPage(kTanPaper);

  // Precondition for the regression: the tint alone trips the color detector.
  BOOST_REQUIRE(LeptonicaDetector::detect(page) == LeptonicaDetector::ColorType::Color);

  const LeptonicaDetector::ColorType compensated = LeptonicaDetector::detectWithCastCompensation(page);
  BOOST_CHECK(compensated != LeptonicaDetector::ColorType::Color);
  BOOST_CHECK(compensated == LeptonicaDetector::ColorType::BlackWhite);
}

// A genuine color photo on the same tan paper must survive neutralization
// and stay COLOR.
BOOST_AUTO_TEST_CASE(color_photo_on_tan_page_stays_color) {
  const QImage page = makeColorPhotoOnTanPage();

  const LeptonicaDetector::ColorType compensated = LeptonicaDetector::detectWithCastCompensation(page);
  BOOST_CHECK(compensated == LeptonicaDetector::ColorType::Color);
}

BOOST_AUTO_TEST_CASE(grayscale_photo_on_text_page_is_mixed) {
  const QImage page = makeGrayscalePhotoOnTextPage();
  LeptonicaDetector::DetectionEvidence evidence;
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page, 8, &evidence)
              == LeptonicaDetector::ColorType::Mixed);
  BOOST_CHECK(!evidence.largestRegionBounds.isEmpty());
  BOOST_CHECK(evidence.largestRegionBounds.left() >= 0);
  BOOST_CHECK(evidence.largestRegionBounds.top() >= 0);
  BOOST_CHECK(evidence.largestRegionBounds.right() < evidence.analysisSize.width());
  BOOST_CHECK(evidence.largestRegionBounds.bottom() < evidence.analysisSize.height());
  BOOST_CHECK(evidence.largestRegionTileCount >= 2);
  BOOST_CHECK(!evidence.regionBounds.isEmpty());
}

BOOST_AUTO_TEST_CASE(full_page_grayscale_photo_preserves_tone) {
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(makeFullPageGrayscalePhoto())
              != LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(full_page_color_photo_stays_color) {
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(makeFullPageColorPhoto())
              == LeptonicaDetector::ColorType::Color);
}

BOOST_AUTO_TEST_CASE(small_grayscale_inset_is_not_mixed) {
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(makeSmallGrayscaleInset(false))
              == LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(edge_grayscale_inset_is_not_mixed) {
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(makeSmallGrayscaleInset(true))
              == LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(sparse_gray_noise_is_not_mixed) {
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(makeTextPageWithSparseGrayNoise())
              == LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(line_art_woodcut_stipple_and_screen_stay_document) {
  for (int style = 0; style < 3; ++style) {
    BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(makeLineArtPage(style))
                == LeptonicaDetector::ColorType::BlackWhite);
  }
}

BOOST_AUTO_TEST_CASE(integrated_halftone_remains_photo_like) {
  LeptonicaDetector::AnalysisScalePolicy integrationScale;
  integrationScale.targetLongEdge = 250;
  integrationScale.minimumShortEdge = 100;
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(
                  makeIntegratedHalftonePage(), 8, nullptr, integrationScale)
              != LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(nearby_analysis_scales_preserve_fixture_verdicts) {
  const QImage fixtures[] = {
      makeGrayscalePhotoOnTextPage().scaled(1600, 2000, Qt::IgnoreAspectRatio,
                                            Qt::SmoothTransformation),
      makeFullPageGrayscalePhoto().scaled(1600, 2000, Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation),
      makeLineArtPage(0).scaled(1600, 2000, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation),
      makeIntegratedHalftonePage().scaled(1600, 2000, Qt::IgnoreAspectRatio,
                                          Qt::SmoothTransformation),
  };
  LeptonicaDetector::AnalysisScalePolicy lower;
  lower.targetLongEdge = 1100;
  LeptonicaDetector::AnalysisScalePolicy upper;
  upper.targetLongEdge = 1300;
  for (const QImage& fixture : fixtures) {
    BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(fixture, 8, nullptr, lower)
                == LeptonicaDetector::detectWithCastCompensation(fixture, 8, nullptr, upper));
  }
}

BOOST_AUTO_TEST_CASE(evidence_collection_preserves_current_verdicts) {
  const QImage pages[] = {
      makeTextPage(QColor(250, 250, 250)),
      makeGrayscalePhotoOnTextPage(),
      makeFullPageGrayscalePhoto(),
      makeFullPageColorPhoto(),
      makeSmallGrayscaleInset(false),
      makeSmallGrayscaleInset(true),
      makeTextPageWithSparseGrayNoise(),
      makeTextPage(kTanPaper),
      makeColorPhotoOnTanPage(),
  };

  for (const QImage& page : pages) {
    const auto withoutEvidence = LeptonicaDetector::detectWithCastCompensation(page, 8);
    LeptonicaDetector::DetectionEvidence evidence;
    const auto withEvidence =
        LeptonicaDetector::detectWithCastCompensation(page, 8, &evidence);
    BOOST_CHECK(withEvidence == withoutEvidence);
    BOOST_CHECK(evidence.rawVerdict == withoutEvidence);
    BOOST_CHECK(LeptonicaDetector::classify(evidence) == withoutEvidence);
    BOOST_CHECK(evidence.detectorSchemaVersion
                == LeptonicaDetector::DETECTOR_SCHEMA_VERSION);
    BOOST_CHECK(evidence.analysisSize == page.size());
    BOOST_CHECK(evidence.sourceSize == page.size());
    BOOST_CHECK(!evidence.scaleApplied);
  }
}

BOOST_AUTO_TEST_CASE(explicit_analysis_cap_records_scale_without_changing_default) {
  const QImage page = makeGrayscalePhotoOnTextPage();
  LeptonicaDetector::AnalysisScalePolicy cap;
  cap.targetLongEdge = 600;
  LeptonicaDetector::DetectionEvidence capped;
  LeptonicaDetector::detectWithCastCompensation(page, 8, &capped, cap);

  BOOST_CHECK(capped.sourceSize == page.size());
  BOOST_CHECK(capped.analysisSize == QSize(480, 600));
  BOOST_CHECK(capped.requestedLongEdgeCap == 600);
  BOOST_CHECK(capped.minimumShortEdge == 300);
  BOOST_CHECK(capped.scaleApplied);

  LeptonicaDetector::DetectionEvidence uncapped;
  LeptonicaDetector::AnalysisScalePolicy noCap;
  LeptonicaDetector::detectWithCastCompensation(page, 8, &uncapped, noCap);
  BOOST_CHECK(uncapped.analysisSize == page.size());
  BOOST_CHECK(!uncapped.scaleApplied);
}

BOOST_AUTO_TEST_CASE(detection_honors_phase_cancellation) {
  const QImage page = makeGrayscalePhotoOnTextPage();
  // Exercise cancellation before conversion, with a live converted PIX, with
  // a live normalized grayscale PIX / histogram, and after feature collection.
  // Detector-side RAII must release every Leptonica allocation while the
  // exception propagates.
  for (int checksBeforeCancel = 0; checksBeforeCancel <= 4; ++checksBeforeCancel) {
    CancellingStatus status(checksBeforeCancel);
    LeptonicaDetector::DetectionEvidence evidence;
    BOOST_CHECK_THROW(
        LeptonicaDetector::detectWithCastCompensation(
            page, 8, &evidence, LeptonicaDetector::AnalysisScalePolicy(), &status),
        std::runtime_error);
  }
}

BOOST_AUTO_TEST_CASE(evidence_reports_normalized_global_and_all_interior_cells) {
  const QImage page = makeGrayscalePhotoOnTextPage();
  LeptonicaDetector::DetectionEvidence evidence;
  const auto verdict = LeptonicaDetector::detectWithCastCompensation(page, 10, &evidence);

  BOOST_REQUIRE(verdict == LeptonicaDetector::ColorType::Mixed);
  BOOST_CHECK_CLOSE(evidence.darkRatio + evidence.midtoneRatio + evidence.lightRatio,
                    1.0f, 0.001f);
  BOOST_CHECK_CLOSE(evidence.cellThreshold, 0.50f, 0.001f);
  int aboveThreshold = 0;
  int index = 0;
  for (int gy = 1; gy <= 4; ++gy) {
    for (int gx = 1; gx <= 4; ++gx) {
      const auto& cell = evidence.interiorCells[index++];
      BOOST_CHECK(cell.gridX == gx);
      BOOST_CHECK(cell.gridY == gy);
      BOOST_CHECK(cell.sampled);
      BOOST_CHECK(cell.width > 0);
      BOOST_CHECK(cell.height > 0);
      BOOST_CHECK(cell.midtoneRatio >= 0.0f);
      BOOST_CHECK(cell.midtoneRatio <= 1.0f);
      if (cell.aboveThreshold) {
        ++aboveThreshold;
      }
    }
  }
  BOOST_CHECK(evidence.highMidtoneCellCount == aboveThreshold);
}

BOOST_AUTO_TEST_CASE(evidence_reports_cast_compensation_path_and_raw_verdict) {
  const QImage page = makeTextPage(kTanPaper);
  LeptonicaDetector::DetectionEvidence evidence;
  const auto verdict = LeptonicaDetector::detectWithCastCompensation(page, 10, &evidence);

  BOOST_REQUIRE(verdict == LeptonicaDetector::ColorType::BlackWhite);
  BOOST_CHECK(evidence.preCastVerdict == LeptonicaDetector::ColorType::Color);
  BOOST_CHECK(evidence.rawVerdict == LeptonicaDetector::ColorType::BlackWhite);
  BOOST_CHECK(evidence.preCastColorFraction > evidence.colorFraction);
  BOOST_CHECK(evidence.castPath
              == LeptonicaDetector::DetectionEvidence::CastPath::WhiteBalanceRecheck);
}

// Neutral white paper with black text is B&W, and compensation changes nothing.
BOOST_AUTO_TEST_CASE(white_text_page_is_bw) {
  const QImage page = makeTextPage(QColor(250, 250, 250));

  BOOST_CHECK(LeptonicaDetector::detect(page) == LeptonicaDetector::ColorType::BlackWhite);
  LeptonicaDetector::DetectionEvidence evidence;
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page, 8, &evidence)
              == LeptonicaDetector::ColorType::BlackWhite);
  BOOST_CHECK(evidence.largestRegionBounds.isEmpty());
}

BOOST_AUTO_TEST_CASE(real_toned_text_crop_is_bw) {
  const QString path =
      QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/toned-text-page-92.png");
  BOOST_REQUIRE(QFileInfo::exists(path));
  const QImage page(path);
  BOOST_REQUIRE(!page.isNull());

  BOOST_REQUIRE(LeptonicaDetector::detect(page) == LeptonicaDetector::ColorType::Color);
  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page)
              == LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(real_color_card_crop_stays_color) {
  const QString path =
      QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/color-card-page-390.png");
  BOOST_REQUIRE(QFileInfo::exists(path));
  const QImage page(path);
  BOOST_REQUIRE(!page.isNull());

  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page) == LeptonicaDetector::ColorType::Color);
}

BOOST_AUTO_TEST_CASE(reduced_mixed_photo_fixture_is_now_mixed) {
  const QString path = QStringLiteral(
      SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/mixed-photo-bw-failure-480.png");
  if (!QFileInfo::exists(path)) {
    BOOST_TEST_MESSAGE("SKIP: private-only mixed-photo characterization fixture is absent");
    return;
  }
  const QImage page(path);
  BOOST_REQUIRE(!page.isNull());
  BOOST_CHECK_LE(std::max(page.width(), page.height()), 480);

  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page, 8)
              == LeptonicaDetector::ColorType::Mixed);
}

BOOST_AUTO_TEST_CASE(reduced_photo_plate_fixture_is_no_longer_bw) {
  const QString path = QStringLiteral(
      SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/photo-plate-bw-failure-480.png");
  if (!QFileInfo::exists(path)) {
    BOOST_TEST_MESSAGE("SKIP: private-only photo-plate characterization fixture is absent");
    return;
  }
  const QImage page(path);
  BOOST_REQUIRE(!page.isNull());
  BOOST_CHECK_LE(std::max(page.width(), page.height()), 480);

  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page, 8)
              != LeptonicaDetector::ColorType::BlackWhite);
}

BOOST_AUTO_TEST_CASE(reduced_photo_plate_fixture_stays_grayscale) {
  const QString path = QStringLiteral(
      SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/photo-plate-grayscale-success-480.png");
  if (!QFileInfo::exists(path)) {
    BOOST_TEST_MESSAGE("SKIP: private-only photo-plate characterization fixture is absent");
    return;
  }
  const QImage page(path);
  BOOST_REQUIRE(!page.isNull());
  BOOST_CHECK_LE(std::max(page.width(), page.height()), 480);

  BOOST_CHECK(LeptonicaDetector::detectWithCastCompensation(page, 8)
              == LeptonicaDetector::ColorType::Grayscale);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace Tests
