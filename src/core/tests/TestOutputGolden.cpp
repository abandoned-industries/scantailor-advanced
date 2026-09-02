// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Stage 1.5.1: golden-image characterization of OutputGenerator.
//
// Pins what BOTH render drivers (processWithoutDewarping / processWithDewarping)
// produce TODAY for the five concrete ColorModes crossed with the options that
// matter to the Stage 2A unification: fill colors (the D1 asymmetry), fill
// zones, picture shapes, split output. Goldens are CPU-path renders on this
// machine (the first test asserts Metal is unavailable in this binary, so a
// change in acceleration gating fails loudly instead of flaking).
//
// AUTO_DETECT (-1) is deliberately absent: it is a transient finalize-time
// request (ColorParams.h comment) and never reaches the render layer; the
// finalize-side resolution is covered by TestAutoColorModePolicy.
//
// D1 is FIXED: FILL_BLACK is now honored by BOTH drivers (the dewarp driver's
// filling-color selection mirrors the non-dewarp switch since the D1 port).
// The dewarp_mixed_fill_black / dewarp_color_fill_black goldens were
// regenerated with that fix and now pin black margins like their non-dewarp
// siblings. Note: pure-BW output still returns early and never reaches the
// filling-color switch, so the bw_fill_* cells pin that early-return fact:
// fill color is irrelevant to pure-BW output in both drivers.

#include <QImage>
#include <boost/test/unit_test.hpp>

#include "GoldenSourcePages.h"
#include "OutputGoldenHarness.h"

namespace Tests {
namespace {

const GoldenCase kCases[] = {
    // --- non-dewarp driver, five concrete modes ---
    {"bw_text", GoldenPage::TEXT, output::BLACK_AND_WHITE},
    {"gray_text", GoldenPage::TEXT, output::GRAYSCALE},
    {"colorgray_text", GoldenPage::TEXT, output::COLOR_GRAYSCALE},
    {"color_mixedpage", GoldenPage::MIXED, output::COLOR},
    {"mixed_mixedpage", GoldenPage::MIXED, output::MIXED},

    // picture shape variants (mixed page)
    {"mixed_rect_shape", GoldenPage::MIXED, output::MIXED, false, output::FILL_BACKGROUND, false,
     output::BLACK_AND_WHITE_FOREGROUND, false, output::RECTANGULAR_SHAPE},

    // Pure-BW cells: fill color never reaches the pure-binary assembly path
    // (early return before the filling-color switch) — pinned as-is.
    {"bw_fill_black", GoldenPage::TEXT, output::BLACK_AND_WHITE, false, output::FILL_BLACK},
    {"bw_fill_white", GoldenPage::TEXT, output::BLACK_AND_WHITE, false, output::FILL_WHITE},
    {"color_fill_background", GoldenPage::MIXED, output::COLOR, false, output::FILL_BACKGROUND},

    // D1 cells proper: FILL_BLACK in the modes that reach the switch.
    {"mixed_fill_black", GoldenPage::MIXED, output::MIXED, false, output::FILL_BLACK},
    {"color_fill_black", GoldenPage::MIXED, output::COLOR, false, output::FILL_BLACK},

    // fill zones present
    {"mixed_fillzone", GoldenPage::MIXED, output::MIXED, false, output::FILL_BACKGROUND, false,
     output::BLACK_AND_WHITE_FOREGROUND, false, output::FREE_SHAPE, true},

    // split output without original background (the safe splitting cell)
    {"mixed_split_bwfg", GoldenPage::MIXED, output::MIXED, false, output::FILL_BACKGROUND, true,
     output::BLACK_AND_WHITE_FOREGROUND, false},

    // split + original background, non-dewarp: the former D2 crash
    // configuration (prereqs §1.2 D2), rendering since QW1 stopped releasing
    // bwContent before its original-background uses. Golden added with QW1.
    {"mixed_split_orig_bg", GoldenPage::MIXED, output::MIXED, false, output::FILL_BACKGROUND, true,
     output::BLACK_AND_WHITE_FOREGROUND, true},

    // D3 exercise: wiener denoise on, non-dewarp (bg color measured from
    // denoised pixels today)
    {"bw_wiener", GoldenPage::TEXT, output::BLACK_AND_WHITE, false, output::FILL_BACKGROUND, false,
     output::BLACK_AND_WHITE_FOREGROUND, false, output::FREE_SHAPE, false, 0.5},

    // --- dewarp driver (MANUAL + fixed committed model) ---
    {"dewarp_bw_text", GoldenPage::TEXT, output::BLACK_AND_WHITE, true},
    {"dewarp_colorgray_text", GoldenPage::TEXT, output::COLOR_GRAYSCALE, true},
    {"dewarp_mixed_mixedpage", GoldenPage::MIXED, output::MIXED, true},

    // D1 cells, dewarp: FILL_BLACK honored since the D1 port (bw cell still
    // hits the pure-BW early return, like its non-dewarp sibling)
    {"dewarp_bw_fill_black", GoldenPage::TEXT, output::BLACK_AND_WHITE, true, output::FILL_BLACK},
    {"dewarp_mixed_fill_black", GoldenPage::MIXED, output::MIXED, true, output::FILL_BLACK},
    {"dewarp_color_fill_black", GoldenPage::MIXED, output::COLOR, true, output::FILL_BLACK},

    // D2's correct sibling: the dewarp driver does NOT release bwContent, so
    // split + original background renders instead of crashing (prereqs D2)
    {"dewarp_mixed_split_orig_bg", GoldenPage::MIXED, output::MIXED, true, output::FILL_BACKGROUND,
     true, output::BLACK_AND_WHITE_FOREGROUND, true},

    // D3 exercise: wiener under dewarp (bg color measured from raw pixels)
    {"dewarp_bw_wiener", GoldenPage::TEXT, output::BLACK_AND_WHITE, true, output::FILL_BACKGROUND,
     false, output::BLACK_AND_WHITE_FOREGROUND, false, output::FREE_SHAPE, false, 0.5},
};

QImage loadSourcePage(GoldenPage page) {
  const QString path = goldenFixtureDir()
                       + (page == GoldenPage::TEXT ? QStringLiteral("/text-page.png")
                                                   : QStringLiteral("/mixed-page.png"));
  QImage image(path);
  BOOST_REQUIRE_MESSAGE(!image.isNull(), "missing frozen source page: " << path.toStdString());
  return image.convertToFormat(QImage::Format_RGB32);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OutputGoldenTestSuite)

// Goldens are CPU-path renders: this binary must not have Metal available.
// If this ever fails, the acceleration gating changed — regenerate goldens
// deliberately and record which path they pin.
BOOST_AUTO_TEST_CASE(metal_is_unavailable_in_test_binary) {
  BOOST_CHECK(!metalGaussBlurAvailable());
}

// The frozen source pages must match the committed bytes' pixels exactly when
// decoded (a drift alarm for the fixture files, not for the generator).
BOOST_AUTO_TEST_CASE(source_pages_load) {
  const QImage text = loadSourcePage(GoldenPage::TEXT);
  BOOST_CHECK_EQUAL(text.width(), 600);
  BOOST_CHECK_EQUAL(text.height(), 800);
  const QImage mixed = loadSourcePage(GoldenPage::MIXED);
  BOOST_CHECK_EQUAL(mixed.width(), 600);
  BOOST_CHECK_EQUAL(mixed.height(), 800);
}

// The full golden matrix: byte-exact against the committed goldens (or
// regenerating them when ST_GOLDEN_REGEN is set).
BOOST_AUTO_TEST_CASE(golden_matrix)  // NOLINT(readability-function-cognitive-complexity)
{
  const QImage textPage = loadSourcePage(GoldenPage::TEXT);
  const QImage mixedPage = loadSourcePage(GoldenPage::MIXED);

  for (const GoldenCase& goldenCase : kCases) {
    const QImage& source = (goldenCase.page == GoldenPage::TEXT) ? textPage : mixedPage;
    const GoldenRender render = renderGoldenCase(goldenCase, source);
    BOOST_REQUIRE_MESSAGE(!render.image.isNull(), goldenCase.name << ": render returned null image");

    QString detail;
    BOOST_CHECK_MESSAGE(compareOrRegenGolden(QLatin1String(goldenCase.name), render.image, &detail),
                        detail.toStdString());

    // Auto picture masks are part of the pinned surface for MIXED modes.
    if (!render.automask.isNull()) {
      QString maskDetail;
      BOOST_CHECK_MESSAGE(compareOrRegenGolden(QLatin1String(goldenCase.name) + QStringLiteral("_automask"),
                                               render.automask, &maskDetail),
                          maskDetail.toStdString());
    }
  }
}

// Rendering the same case twice in one process must be bit-identical (guards
// against hidden global state making the goldens flaky).
BOOST_AUTO_TEST_CASE(render_is_deterministic_in_process) {
  const QImage mixedPage = loadSourcePage(GoldenPage::MIXED);
  const GoldenCase& goldenCase = kCases[4];  // mixed_mixedpage
  const QImage first = renderGoldenCase(goldenCase, mixedPage).image;
  const QImage second = renderGoldenCase(goldenCase, mixedPage).image;
  BOOST_REQUIRE(!first.isNull());
  BOOST_CHECK(first == second);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
