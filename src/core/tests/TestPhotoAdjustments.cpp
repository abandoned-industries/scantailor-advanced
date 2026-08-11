// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <QDomDocument>
#include <QImage>

#include <boost/test/unit_test.hpp>
#include <cmath>

#include "weasel/PhotoAdjustments.h"
#include "weasel/TonalCurve.h"

BOOST_AUTO_TEST_SUITE(PhotoAdjustmentsTestSuite)

BOOST_AUTO_TEST_CASE(exposure_accepts_hundredths_and_clamps_to_one_stop) {
  weasel::PhotoAdjustments adjustments;
  adjustments.setExposure(0.01);
  BOOST_CHECK_CLOSE(adjustments.exposure(), 0.01, 0.0001);

  adjustments.setExposure(4.25);
  BOOST_CHECK_EQUAL(adjustments.exposure(), 1.0);
  adjustments.setExposure(-3.5);
  BOOST_CHECK_EQUAL(adjustments.exposure(), -1.0);
}

BOOST_AUTO_TEST_CASE(legacy_exposure_values_clamp_without_reinterpretation) {
  QDomDocument document;
  BOOST_REQUIRE(document.setContent(QStringLiteral("<photo exposure=\"3.75\"/>")));
  const weasel::PhotoAdjustments positive(document.documentElement());
  BOOST_CHECK_EQUAL(positive.exposure(), 1.0);

  BOOST_REQUIRE(document.setContent(QStringLiteral("<photo exposure=\"-2.5\"/>")));
  const weasel::PhotoAdjustments negative(document.documentElement());
  BOOST_CHECK_EQUAL(negative.exposure(), -1.0);
}

BOOST_AUTO_TEST_CASE(exposure_hundredths_round_trip_through_xml) {
  weasel::PhotoAdjustments adjustments;
  adjustments.setExposure(0.37);

  QDomDocument document;
  const weasel::PhotoAdjustments restored(
      adjustments.toXml(document, QStringLiteral("photo")));
  BOOST_CHECK_CLOSE(restored.exposure(), 0.37, 0.0001);
}

BOOST_AUTO_TEST_CASE(exposure_keeps_f_stop_rendering_semantics_at_fine_precision) {
  QImage image(1, 1, QImage::Format_Grayscale8);
  image.scanLine(0)[0] = 200;

  const QImage unchanged = weasel::TonalCurve::apply(image, 0, 0, 0, 0, 0, 0, 0, 0);
  const QImage plusHundredth = weasel::TonalCurve::apply(image, 0, 0, 0.01, 0, 0, 0, 0, 0);
  const QImage minusHundredth = weasel::TonalCurve::apply(image, 0, 0, -0.01, 0, 0, 0, 0, 0);
  const QImage minusOne = weasel::TonalCurve::apply(image, 0, 0, -1.0, 0, 0, 0, 0, 0);

  BOOST_CHECK_EQUAL(unchanged.constScanLine(0)[0], 200);
  BOOST_CHECK_EQUAL(plusHundredth.constScanLine(0)[0],
                    static_cast<unsigned char>(200.0 * std::pow(2.0, 0.01)));
  BOOST_CHECK_EQUAL(minusHundredth.constScanLine(0)[0],
                    static_cast<unsigned char>(200.0 * std::pow(2.0, -0.01)));
  BOOST_CHECK_EQUAL(minusOne.constScanLine(0)[0], 100);
  BOOST_CHECK_GT(plusHundredth.constScanLine(0)[0], unchanged.constScanLine(0)[0]);
  BOOST_CHECK_LT(minusHundredth.constScanLine(0)[0], unchanged.constScanLine(0)[0]);
}

BOOST_AUTO_TEST_CASE(auto_exposure_respects_the_manual_range) {
  QImage darkImage(10, 10, QImage::Format_Grayscale8);
  darkImage.fill(16);
  const weasel::TonalCurve::AutoResult result = weasel::TonalCurve::autoDetect(darkImage);
  BOOST_CHECK_GE(result.exposure, weasel::PhotoAdjustments::MIN_EXPOSURE);
  BOOST_CHECK_LE(result.exposure, weasel::PhotoAdjustments::MAX_EXPOSURE);
  BOOST_CHECK_EQUAL(result.exposure, 1.0);
}

BOOST_AUTO_TEST_SUITE_END()
