// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Synthetic source pages for the Stage 1.5.1 OutputGenerator golden tests.
//
// Both pages are produced with direct pixel arithmetic (no QPainter, no font
// rendering), a fixed LCG seed, and no dependence on Qt's raster engine, so
// the generator is deterministic across Qt versions. The committed PNGs under
// fixtures/golden/ were written once by golden_fixture_writer; tests load
// those frozen bytes rather than regenerating.

#ifndef SCANTAILOR_TESTS_GOLDENSOURCEPAGES_H_
#define SCANTAILOR_TESTS_GOLDENSOURCEPAGES_H_

#include <QImage>

namespace Tests {

// 600x800 "text page": black word-like strokes on off-white paper with a
// horizontal color tint and a corner illumination falloff (exercises
// binarization, illumination normalization, despeckle).
QImage makeGoldenTextPage();

// The text page plus an embedded continuous-tone photo block with a dark
// frame (exercises picture detection, picture shapes, mixed reassembly).
QImage makeGoldenMixedPage();

}  // namespace Tests

#endif  // SCANTAILOR_TESTS_GOLDENSOURCEPAGES_H_
