// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_WEASEL_TONALCURVE_H_
#define SCANTAILOR_WEASEL_TONALCURVE_H_

#include <QImage>
#include <cstdint>

namespace weasel {

class TonalCurve {
 public:
  // Apply tonal adjustments via per-channel LUTs in a single pass.
  // For color images: builds separate R/G/B LUTs incorporating temp/tint.
  // For grayscale: builds a single luminance LUT (temp/tint ignored).
  static QImage apply(const QImage& image,
                      double temp,        // -100 to +100
                      double tint,        // -100 to +100
                      double exposure,    // -1.0 to +1.0 f-stops
                      double contrast,    // -100 to +100
                      double highlights,  // -100 to +100
                      double shadows,     // -100 to +100
                      double whites,      // -100 to +100
                      double blacks);     // -100 to +100

  // Analyze image histogram and compute suggested slider values.
  // For grayscale images, temp/tint will be 0.
  struct AutoResult {
    double temp = 0;
    double tint = 0;
    double exposure = 0;
    double contrast = 0;
    double highlights = 0;
    double shadows = 0;
    double whites = 0;
    double blacks = 0;
  };

  static AutoResult autoDetect(const QImage& image);

 private:
  // Build a luminance LUT for the 6 tonal parameters
  static void buildToneLUT(uint8_t lut[256],
                           double exposure,
                           double contrast,
                           double highlights,
                           double shadows,
                           double whites,
                           double blacks);

  // Compute per-channel multipliers for temp/tint
  static void tempTintMultipliers(double temp, double tint,
                                  double& rMult, double& gMult, double& bMult);

  static double smoothstep(double edge0, double edge1, double x);
};

}  // namespace weasel

#endif  // SCANTAILOR_WEASEL_TONALCURVE_H_
