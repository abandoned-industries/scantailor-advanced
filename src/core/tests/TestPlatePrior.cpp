// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license.

#include <PlatePrior.h>

#include <QImage>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(PlatePriorTestSuite)

BOOST_AUTO_TEST_CASE(rejects_text_like_page) {
  QImage image(1000, 1400, QImage::Format_Grayscale8);
  image.fill(245);
  for (int y = 100; y < 1300; y += 28) {
    for (int x = 100; x < 900; ++x) {
      if ((x / 18) % 5 != 4) image.scanLine(y)[x] = 25;
    }
  }
  const auto evidence = PlatePrior::analyze(image);
  BOOST_CHECK(!evidence.isPlate);
}

BOOST_AUTO_TEST_CASE(accepts_spatially_varied_continuous_tone) {
  QImage image(1000, 1000, QImage::Format_Grayscale8);
  for (int y = 0; y < image.height(); ++y) {
    uchar* line = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      line[x] = static_cast<uchar>(25 + ((x * 13 + y * 7 + (x * y) / 31) % 205));
    }
  }
  const auto evidence = PlatePrior::analyze(image);
  BOOST_CHECK(evidence.isPlate);
  BOOST_CHECK(evidence.reason == QStringLiteral("plate_prior_continuous_tone_no_text_structure"));
}

BOOST_AUTO_TEST_CASE(accepts_caption_with_bounded_interior_vision_noise) {
  QVector<QRectF> regions;
  regions << QRectF(400, 880, 350, 18)
          << QRectF(250, 420, 40, 30)
          << QRectF(700, 600, 70, 45);
  const auto evidence =
      PlatePrior::analyzeCaptionText(regions, QSize(1000, 1000));
  BOOST_CHECK(evidence.isCaptionScale);
  BOOST_CHECK_EQUAL(evidence.peripheralRegionCount, 1);
}

BOOST_AUTO_TEST_CASE(rejects_document_scale_vision_text) {
  QVector<QRectF> regions;
  for (int y = 100; y < 900; y += 30) {
    regions << QRectF(100, y, 780, 18);
  }
  const auto evidence =
      PlatePrior::analyzeCaptionText(regions, QSize(1000, 1000));
  BOOST_CHECK(!evidence.isCaptionScale);
}

BOOST_AUTO_TEST_CASE(spread_geometry_requires_wide_page) {
  QImage square(1000, 1000, QImage::Format_Grayscale8);
  square.fill(240);
  BOOST_CHECK(!PlatePrior::analyzeSpreadGeometry(square).isSpread);

  QImage wide(1800, 1000, QImage::Format_Grayscale8);
  wide.fill(240);
  BOOST_CHECK(PlatePrior::analyzeSpreadGeometry(wide).isSpread);
}

BOOST_AUTO_TEST_CASE(near_square_records_gutter_without_relaxing_spread_shape) {
  QImage image(1050, 1000, QImage::Format_Grayscale8);
  for (int y = 0; y < image.height(); ++y) {
    uchar* line = image.scanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      const bool gutter = x >= 515 && x < 535;
      line[x] = gutter
          ? 245
          : static_cast<uchar>(30 + ((x * 17 + y * 11) % 190));
    }
  }

  const auto evidence = PlatePrior::analyzeSpreadGeometry(image);
  BOOST_CHECK(evidence.hasFullHeightGutter);
  BOOST_CHECK(!evidence.isSpread);
  BOOST_CHECK(evidence.reason
              == QStringLiteral("spread_geometry_near_square_full_height_gutter"));
}

BOOST_AUTO_TEST_SUITE_END()
