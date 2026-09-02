// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// D2 regression test (prereqs §1.2 D2; QW1 / Stage 0.9(a)).
//
// Before QW1 the non-dewarp driver released `bwContent` and then used it on
// the ORIGINAL_BACKGROUND path (OutputGenerator.cpp release-before-use at the
// former line 1558 vs 1564/1572) — a null-pointer dereference this suite used
// to pin by running the configuration in a child process and asserting the
// SIGSEGV. QW1 mirrored the correct dewarp-driver shape (which never releases
// `dewarpedBwContent` before its uses), so the configuration now renders.
//
// This test runs the formerly crashing configuration (MIXED + split output +
// B&W foreground + original background, non-dewarp) IN-PROCESS and asserts a
// normal render. Pixel-exactness is pinned by the `mixed_split_orig_bg` golden
// cell in TestOutputGolden.cpp; here we additionally cross-check the result
// against its dewarp sibling golden (`dewarp_mixed_split_orig_bg`) for
// structural plausibility — same geometry and a comparable amount of dark
// content — NOT pixel equality (the sibling is warped by the fixed manual
// distortion model).

#include <QImage>
#include <boost/test/unit_test.hpp>

#include "GoldenSourcePages.h"
#include "OutputGoldenHarness.h"

namespace Tests {
namespace {

// Fraction of pixels darker than mid-gray (luminance < 128).
double darkFraction(const QImage& image) {
  const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
  qint64 dark = 0;
  for (int y = 0; y < gray.height(); ++y) {
    const uchar* line = gray.constScanLine(y);
    for (int x = 0; x < gray.width(); ++x) {
      if (line[x] < 128) {
        ++dark;
      }
    }
  }
  return static_cast<double>(dark) / (static_cast<qint64>(gray.width()) * gray.height());
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OutputD2ProbeTestSuite)

BOOST_AUTO_TEST_CASE(d2_configuration_renders) {
  GoldenCase probe{"mixed_split_orig_bg", GoldenPage::MIXED, output::MIXED};
  probe.splitOutput = true;
  probe.splittingMode = output::BLACK_AND_WHITE_FOREGROUND;
  probe.originalBackground = true;

  const GoldenRender render = renderGoldenCase(probe, makeGoldenMixedPage());
  BOOST_REQUIRE_MESSAGE(!render.image.isNull(),
                        "the former D2 crash configuration no longer renders (QW1 regression)");

  // Structural cross-check against the dewarp sibling golden: same canvas,
  // and a comparable dark-content fraction (structure, not pixel equality —
  // the sibling is dewarped).
  const QString siblingPath
      = goldenFixtureDir() + QStringLiteral("/goldens/dewarp_mixed_split_orig_bg.png");
  const QImage sibling(siblingPath);
  BOOST_REQUIRE_MESSAGE(!sibling.isNull(), "missing sibling golden: " << siblingPath.toStdString());

  BOOST_CHECK_EQUAL(render.image.width(), sibling.width());
  BOOST_CHECK_EQUAL(render.image.height(), sibling.height());

  const double renderedDark = darkFraction(render.image);
  const double siblingDark = darkFraction(sibling);
  BOOST_TEST_MESSAGE("dark fraction: rendered=" << renderedDark << " dewarp sibling=" << siblingDark);
  BOOST_CHECK_MESSAGE(std::abs(renderedDark - siblingDark) < 0.15,
                      "dark-content fraction implausibly far from the dewarp sibling ("
                          << renderedDark << " vs " << siblingDark << ")");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
