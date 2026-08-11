// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TonalCurve.h"

#include "PhotoAdjustments.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace weasel {

double TonalCurve::smoothstep(double edge0, double edge1, double x) {
  double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

void TonalCurve::tempTintMultipliers(double temp, double tint,
                                     double& rMult, double& gMult, double& bMult) {
  // temp: positive = warmer (boost red, cut blue), negative = cooler
  // tint: positive = magenta (cut green), negative = green (boost green)
  // Scale: at +/-100, shift channels by ~30%
  const double tempScale = temp / 100.0 * 0.30;
  const double tintScale = tint / 100.0 * 0.30;

  rMult = 1.0 + tempScale;
  bMult = 1.0 - tempScale;
  gMult = 1.0 - tintScale;
}

void TonalCurve::buildToneLUT(uint8_t lut[256],
                               double exposure,
                               double contrast,
                               double highlights,
                               double shadows,
                               double whites,
                               double blacks) {
  // Order mirrors Lightroom's Basic panel: exposure sets overall brightness,
  // contrast shapes midtones, highlights/shadows recover local detail, then
  // whites/blacks define the final clipping points.
  for (int i = 0; i < 256; ++i) {
    double val = i / 255.0;

    // 1. Exposure: f-stop model
    if (exposure != 0.0) {
      val = val * std::pow(2.0, exposure);
    }

    // 2. Contrast: S-curve around midpoint
    if (contrast != 0.0) {
      double factor = std::pow(2.0, contrast / 100.0 * 1.3);
      val = 0.5 + (val - 0.5) * factor;
    }

    // 3. Highlights: affect bright areas
    if (highlights != 0.0) {
      double weight = smoothstep(0.25, 1.0, val);
      double shift = -highlights / 100.0 * 0.40 * weight;
      val += shift;
    }

    // 4. Shadows: affect dark areas
    if (shadows != 0.0) {
      double weight = 1.0 - smoothstep(0.0, 0.75, val);
      double shift = shadows / 100.0 * 0.40 * weight;
      val += shift;
    }

    // 5. Whites: lower the white point (negative = darker whites)
    if (whites != 0.0) {
      double wp = whites / 100.0 * 0.25;
      if (wp > 0.0) {
        // Positive: push whites brighter (compress toward white)
        val = val + (1.0 - val) * wp * smoothstep(0.5, 1.0, val);
      } else {
        // Negative: pull whites down
        val = val + val * wp * smoothstep(0.5, 1.0, val);
      }
    }

    // 6. Blacks: raise the black point (positive = lighter darks)
    if (blacks != 0.0) {
      double bp = blacks / 100.0 * 0.25;  // max shift of 25% of range
      if (bp > 0.0) {
        // Positive: lift blacks
        val = bp + val * (1.0 - bp);
      } else {
        // Negative: crush blacks
        val = std::max(0.0, val + bp) / (1.0 + bp);
      }
    }

    lut[i] = static_cast<uint8_t>(std::clamp(val * 255.0, 0.0, 255.0));
  }
}

QImage TonalCurve::apply(const QImage& image,
                          double temp, double tint,
                          double exposure, double contrast,
                          double highlights, double shadows,
                          double whites, double blacks) {
  if (image.isNull()) return image;

  const bool isGrayscale = (image.format() == QImage::Format_Grayscale8
                            || image.format() == QImage::Format_Indexed8);

  // Build the base tone LUT
  uint8_t baseLUT[256];
  buildToneLUT(baseLUT, exposure, contrast, highlights, shadows, whites, blacks);

  if (isGrayscale) {
    // Single-channel: apply base LUT directly
    QImage result = image.copy();
    for (int y = 0; y < result.height(); ++y) {
      uint8_t* line = result.scanLine(y);
      for (int x = 0; x < result.width(); ++x) {
        line[x] = baseLUT[line[x]];
      }
    }
    return result;
  }

  // Color image: build per-channel LUTs combining tone + temp/tint
  uint8_t rLUT[256], gLUT[256], bLUT[256];

  double rMult = 1.0, gMult = 1.0, bMult = 1.0;
  if (temp != 0.0 || tint != 0.0) {
    tempTintMultipliers(temp, tint, rMult, gMult, bMult);
  }

  for (int i = 0; i < 256; ++i) {
    double base = baseLUT[i] / 255.0;
    rLUT[i] = static_cast<uint8_t>(std::clamp(base * rMult * 255.0, 0.0, 255.0));
    gLUT[i] = static_cast<uint8_t>(std::clamp(base * gMult * 255.0, 0.0, 255.0));
    bLUT[i] = static_cast<uint8_t>(std::clamp(base * bMult * 255.0, 0.0, 255.0));
  }

  QImage result = image.convertToFormat(QImage::Format_RGB32);
  for (int y = 0; y < result.height(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < result.width(); ++x) {
      const QRgb px = line[x];
      line[x] = qRgb(rLUT[qRed(px)], gLUT[qGreen(px)], bLUT[qBlue(px)]);
    }
  }
  return result;
}

TonalCurve::AutoResult TonalCurve::autoDetect(const QImage& image) {
  AutoResult result;
  if (image.isNull()) return result;

  // Build luminance histogram
  int histogram[256] = {};
  const int totalPixels = image.width() * image.height();

  const QImage img = image.format() == QImage::Format_RGB32
                         ? image
                         : image.convertToFormat(QImage::Format_RGB32);

  for (int y = 0; y < img.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      const QRgb px = line[x];
      int lum = (qRed(px) * 299 + qGreen(px) * 587 + qBlue(px) * 114) / 1000;
      histogram[lum]++;
    }
  }

  // Find the 0.5% and 99.5% percentile values (black/white clipping points)
  const int clipCount = totalPixels / 200;  // 0.5%
  int blackPoint = 0, whitePoint = 255;
  {
    int cumulative = 0;
    for (int i = 0; i < 256; ++i) {
      cumulative += histogram[i];
      if (cumulative >= clipCount) {
        blackPoint = i;
        break;
      }
    }
  }
  {
    int cumulative = 0;
    for (int i = 255; i >= 0; --i) {
      cumulative += histogram[i];
      if (cumulative >= clipCount) {
        whitePoint = i;
        break;
      }
    }
  }

  // Compute median luminance
  int medianLum = 128;
  {
    int cumulative = 0;
    for (int i = 0; i < 256; ++i) {
      cumulative += histogram[i];
      if (cumulative >= totalPixels / 2) {
        medianLum = i;
        break;
      }
    }
  }

  // Exposure: gentle correction only if image is notably dark or bright.
  // Target: keep the median close to where it is, just nudge toward 128.
  const double currentMedian = medianLum / 255.0;
  if (currentMedian > 0.01 && currentMedian < 0.99) {
    const double targetMedian = 0.5;  // true middle gray
    double expAdj = std::log2(targetMedian / currentMedian);
    // Only apply half the correction — conservative approach
    expAdj *= 0.5;
    // Only suggest if the shift is meaningful (> 0.2 stops)
    if (std::abs(expAdj) > 0.2) {
      result.exposure = std::clamp(expAdj, PhotoAdjustments::MIN_EXPOSURE,
                                   PhotoAdjustments::MAX_EXPOSURE);
      result.exposure = std::round(result.exposure * 10.0) / 10.0;
    }
  }

  // Whites: gently push white point toward 255 if it's notably low.
  // Only correct if the top of the histogram is far from 255.
  if (whitePoint < 220) {
    result.whites = std::clamp((255.0 - whitePoint) / 255.0 * 100.0, 0.0, 40.0);
    result.whites = std::round(result.whites);
  }

  // Blacks: gently crush if black point is well above 0.
  if (blackPoint > 20) {
    result.blacks = std::clamp(-blackPoint / 255.0 * 80.0, -40.0, 0.0);
    result.blacks = std::round(result.blacks);
  }

  // Highlights/Shadows: very gentle, only if histogram is really bunched up
  int highlightPixels = 0;
  for (int i = 192; i <= 255; ++i) highlightPixels += histogram[i];
  double highlightDensity = static_cast<double>(highlightPixels) / totalPixels;
  if (highlightDensity > 0.35) {
    result.highlights = std::clamp(-(highlightDensity - 0.35) * 100.0, -50.0, 0.0);
    result.highlights = std::round(result.highlights);
  }

  int shadowPixels = 0;
  for (int i = 0; i <= 64; ++i) shadowPixels += histogram[i];
  double shadowDensity = static_cast<double>(shadowPixels) / totalPixels;
  if (shadowDensity > 0.25) {
    result.shadows = std::clamp((shadowDensity - 0.25) * 100.0, 0.0, 50.0);
    result.shadows = std::round(result.shadows);
  }

  // Contrast: based on histogram spread (interquartile range), gentle
  int q1 = 64, q3 = 192;
  {
    int cumulative = 0;
    for (int i = 0; i < 256; ++i) {
      cumulative += histogram[i];
      if (cumulative >= totalPixels / 4) { q1 = i; break; }
    }
  }
  {
    int cumulative = 0;
    for (int i = 0; i < 256; ++i) {
      cumulative += histogram[i];
      if (cumulative >= totalPixels * 3 / 4) { q3 = i; break; }
    }
  }
  double iqr = (q3 - q1) / 255.0;
  if (iqr < 0.3) {
    result.contrast = std::clamp((0.35 - iqr) * 100.0, 0.0, 30.0);
  }
  result.contrast = std::round(result.contrast);

  // Temp/Tint: analyze color cast for color images
  if (image.format() != QImage::Format_Grayscale8 && image.format() != QImage::Format_Indexed8) {
    double rSum = 0, gSum = 0, bSum = 0;
    int sampleCount = 0;

    // Sample bright, low-saturation pixels (likely paper)
    for (int y = 0; y < img.height(); y += 4) {
      const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
      for (int x = 0; x < img.width(); x += 4) {
        const QRgb px = line[x];
        int r = qRed(px), g = qGreen(px), b = qBlue(px);
        int lum = (r * 299 + g * 587 + b * 114) / 1000;
        int maxC = std::max({r, g, b});
        int minC = std::min({r, g, b});

        // Bright, low-saturation pixels = likely paper
        if (lum > 160 && (maxC - minC) < 80) {
          rSum += r;
          gSum += g;
          bSum += b;
          sampleCount++;
        }
      }
    }

    if (sampleCount > 100) {
      double rAvg = rSum / sampleCount;
      double gAvg = gSum / sampleCount;
      double bAvg = bSum / sampleCount;
      double avg = (rAvg + gAvg + bAvg) / 3.0;

      // Temp: if blue is high relative to red, image is cool (suggest warming)
      double rbImbalance = (rAvg - bAvg) / avg;
      result.temp = std::clamp(-rbImbalance * 100.0, -100.0, 100.0);
      result.temp = std::round(result.temp);

      // Tint: if green is high, suggest magenta shift
      double gImbalance = (gAvg - avg) / avg;
      result.tint = std::clamp(gImbalance * 200.0, -100.0, 100.0);
      result.tint = std::round(result.tint);

      // Only suggest meaningful shifts
      if (std::abs(result.temp) < 3) result.temp = 0;
      if (std::abs(result.tint) < 3) result.tint = 0;
    }
  }

  return result;
}

}  // namespace weasel
