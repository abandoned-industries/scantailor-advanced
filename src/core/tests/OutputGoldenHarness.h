// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Harness for the Stage 1.5.1 OutputGenerator golden-image characterization
// tests. Drives OutputGenerator::process exactly as output::Task does, over a
// deterministic case matrix, and compares against goldens committed under
// fixtures/golden/. See fixtures/golden/README.md for the regeneration recipe.

#ifndef SCANTAILOR_TESTS_OUTPUTGOLDENHARNESS_H_
#define SCANTAILOR_TESTS_OUTPUTGOLDENHARNESS_H_

#include <QImage>
#include <QString>

#include "filters/output/ColorParams.h"
#include "filters/output/PictureShapeOptions.h"
#include "filters/output/SplittingOptions.h"

namespace Tests {

enum class GoldenPage { TEXT, MIXED };

struct GoldenCase {
  const char* name;
  GoldenPage page;
  output::ColorMode colorMode;
  bool dewarp = false;                                             // MANUAL + fixed committed model
  output::FillingColor fillingColor = output::FILL_BACKGROUND;
  bool splitOutput = false;
  output::SplittingMode splittingMode = output::BLACK_AND_WHITE_FOREGROUND;
  bool originalBackground = false;
  output::PictureShape pictureShape = output::FREE_SHAPE;
  bool addFillZone = false;
  double wienerCoef = 0.0;
};

struct GoldenRender {
  QImage image;
  QImage automask;  // null unless auto picture detection ran (MIXED modes)
};

// Renders one case through OutputGenerator::process. Deterministic: CPU path
// only (the harness asserts Metal is unavailable), fixed inputs, MANUAL
// distortion model.
GoldenRender renderGoldenCase(const GoldenCase& goldenCase, const QImage& source);

// Directory of committed goldens/source pages (inside the source tree).
QString goldenFixtureDir();

// True when the ST_GOLDEN_REGEN environment variable is set: goldens are
// (re)written instead of compared.
bool goldenRegenRequested();

// Compare `rendered` with the committed golden `name`. In regen mode, writes
// the golden instead and returns true. On mismatch returns false and, for
// diagnosis, writes the differing render next to the golden as
// <name>.actual.png.
bool compareOrRegenGolden(const QString& name, const QImage& rendered, QString* failureDetail);

extern "C" bool metalGaussBlurAvailable(void);

}  // namespace Tests

#endif  // SCANTAILOR_TESTS_OUTPUTGOLDENHARNESS_H_
