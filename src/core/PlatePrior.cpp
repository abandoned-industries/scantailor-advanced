// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license.

#include "PlatePrior.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
double entropy(const std::array<int, 64>& histogram, int count) {
  double value = 0.0;
  for (int bin : histogram) {
    if (!bin) continue;
    const double probability = double(bin) / count;
    value -= probability * std::log2(probability);
  }
  return value;
}

double regionVariance(const QImage& gray, int left, int right) {
  double sum = 0.0;
  double sumSquares = 0.0;
  int count = 0;
  for (int y = 0; y < gray.height(); ++y) {
    const uchar* line = gray.constScanLine(y);
    for (int x = left; x < right; ++x) {
      const double value = line[x];
      sum += value;
      sumSquares += value * value;
      ++count;
    }
  }
  if (!count) return 0.0;
  const double mean = sum / count;
  return std::max(0.0, sumSquares / count - mean * mean);
}
}  // namespace

PlatePrior::Evidence PlatePrior::analyze(const QImage& image) {
  Evidence evidence;
  if (image.isNull()) {
    evidence.reason = QStringLiteral("plate_prior_invalid_image");
    return evidence;
  }

  constexpr int kMaxDimension = 512;
  const QImage gray = image.scaled(kMaxDimension, kMaxDimension, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_Grayscale8);
  std::array<int, 64> globalHistogram{};
  int midtones = 0;
  const int pixels = gray.width() * gray.height();
  for (int y = 0; y < gray.height(); ++y) {
    const uchar* line = gray.constScanLine(y);
    for (int x = 0; x < gray.width(); ++x) {
      const int value = line[x];
      ++globalHistogram[value >> 2];
      if (value >= 20 && value <= 235) ++midtones;
    }
  }
  evidence.entropy = entropy(globalHistogram, pixels);
  evidence.midtoneFraction = double(midtones) / pixels;

  constexpr int kGrid = 6;
  int tonalTiles = 0;
  int sampledTiles = 0;
  for (int gy = 0; gy < kGrid; ++gy) {
    const int top = gy * gray.height() / kGrid;
    const int bottom = (gy + 1) * gray.height() / kGrid;
    for (int gx = 0; gx < kGrid; ++gx) {
      const int left = gx * gray.width() / kGrid;
      const int right = (gx + 1) * gray.width() / kGrid;
      std::array<int, 64> tileHistogram{};
      int tileMidtones = 0;
      int tilePixels = 0;
      for (int y = top; y < bottom; ++y) {
        const uchar* line = gray.constScanLine(y);
        for (int x = left; x < right; ++x) {
          const int value = line[x];
          ++tileHistogram[value >> 2];
          if (value >= 20 && value <= 235) ++tileMidtones;
          ++tilePixels;
        }
      }
      ++sampledTiles;
      if (entropy(tileHistogram, tilePixels) >= 4.6
          && double(tileMidtones) / tilePixels >= 0.35) {
        ++tonalTiles;
      }
    }
  }
  evidence.tonalTileFraction = double(tonalTiles) / sampledTiles;
  evidence.isPlate = evidence.entropy >= 4.05
      && evidence.midtoneFraction >= 0.30
      && evidence.tonalTileFraction >= 0.08
      && (evidence.midtoneFraction < 0.93
          || evidence.tonalTileFraction >= 0.28
          || evidence.entropy >= 4.35);
  evidence.reason =
      evidence.isPlate
          ? QStringLiteral("plate_prior_continuous_tone_no_text_structure")
          : QStringLiteral("plate_prior_rejected_entropy=%1_tonal_tiles=%2_midtone=%3")
                .arg(evidence.entropy, 0, 'f', 3)
                .arg(evidence.tonalTileFraction, 0, 'f', 3)
                .arg(evidence.midtoneFraction, 0, 'f', 3);
  return evidence;
}

PlatePrior::CaptionEvidence PlatePrior::analyzeCaptionText(
    const QVector<QRectF>& textRegions, const QSize& imageSize) {
  CaptionEvidence evidence;
  evidence.regionCount = textRegions.size();
  if (imageSize.isEmpty()) {
    evidence.reason = QStringLiteral("caption_text_invalid_image_size");
    return evidence;
  }

  constexpr double kPeripheralBand = 0.18;
  for (const QRectF& region : textRegions) {
    const QRectF normalized(
        region.x() / imageSize.width(), region.y() / imageSize.height(),
        region.width() / imageSize.width(), region.height() / imageSize.height());
    const double area = std::max(0.0, normalized.width())
        * std::max(0.0, normalized.height());
    evidence.totalAreaFraction += area;
    const double centerY = normalized.center().y();
    if (centerY <= kPeripheralBand || centerY >= 1.0 - kPeripheralBand) {
      ++evidence.peripheralRegionCount;
    } else {
      evidence.interiorAreaFraction += area;
    }
  }

  // Derived from the live-Vision 30-page geometry census. Prior-positive
  // plates had 1-4 observations and at most 2.414% total text area. Their
  // genuine captions all touched the outer 18% horizontal bands; hallucinated
  // OCR inside photographs occupied at most 1.88%. The small margins below
  // keep the rule stable across Vision revisions and raster rounding.
  constexpr int kMaxRegions = 4;
  constexpr double kMaxTotalArea = 0.025;
  constexpr double kMaxInteriorArea = 0.020;
  evidence.isCaptionScale =
      evidence.regionCount <= kMaxRegions
      && evidence.totalAreaFraction <= kMaxTotalArea
      && (evidence.regionCount == 0 || evidence.peripheralRegionCount > 0)
      && evidence.interiorAreaFraction <= kMaxInteriorArea;
  evidence.reason =
      evidence.isCaptionScale
          ? QStringLiteral("caption_text_within_count=4_total=0.025_band=0.18_interior=0.020")
          : QStringLiteral("caption_text_rejected_count=%1_total=%2_peripheral=%3_interior=%4")
                .arg(evidence.regionCount)
                .arg(evidence.totalAreaFraction, 0, 'f', 4)
                .arg(evidence.peripheralRegionCount)
                .arg(evidence.interiorAreaFraction, 0, 'f', 4);
  return evidence;
}

PlatePrior::SpreadEvidence PlatePrior::analyzeSpreadGeometry(const QImage& image) {
  SpreadEvidence evidence;
  if (image.isNull() || image.height() <= 0) {
    evidence.reason = QStringLiteral("spread_geometry_invalid_image");
    return evidence;
  }
  evidence.aspectRatio = double(image.width()) / image.height();
  constexpr double kDirectSpreadAspect = 1.55;
  constexpr double kGutterMinimumAspect = 1.25;
  if (evidence.aspectRatio >= kDirectSpreadAspect) {
    evidence.isSpread = true;
    evidence.reason = QStringLiteral("spread_geometry_wide_aspect");
    return evidence;
  }

  constexpr int kMaxDimension = 512;
  const QImage gray = image.scaled(kMaxDimension, kMaxDimension, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_Grayscale8);
  const int searchLeft = int(gray.width() * 0.35);
  const int searchRight = int(gray.width() * 0.65);
  const int minRun = std::max(3, int(std::ceil(gray.width() * 0.008)));
  int bestStart = -1;
  int bestLength = 0;
  int runStart = -1;
  for (int x = searchLeft; x < searchRight; ++x) {
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int y = 0; y < gray.height(); ++y) {
      const double value = gray.constScanLine(y)[x];
      sum += value;
      sumSquares += value * value;
    }
    const double mean = sum / gray.height();
    const double variance =
        std::max(0.0, sumSquares / gray.height() - mean * mean);
    const bool uniformPaperColumn = mean >= 220.0 && std::sqrt(variance) <= 18.0;
    if (uniformPaperColumn) {
      if (runStart < 0) runStart = x;
    } else if (runStart >= 0) {
      const int length = x - runStart;
      if (length > bestLength) {
        bestStart = runStart;
        bestLength = length;
      }
      runStart = -1;
    }
  }
  if (runStart >= 0 && searchRight - runStart > bestLength) {
    bestStart = runStart;
    bestLength = searchRight - runStart;
  }

  if (bestLength >= minRun) {
    const int center = bestStart + bestLength / 2;
    const int flankWidth = std::max(4, int(gray.width() * 0.12));
    const double leftStdDev =
        std::sqrt(regionVariance(gray, std::max(0, center - flankWidth), center));
    const double rightStdDev =
        std::sqrt(regionVariance(gray, center, std::min(gray.width(), center + flankWidth)));
    if (leftStdDev >= 28.0 && rightStdDev >= 28.0) {
      evidence.hasFullHeightGutter = true;
      evidence.gutterCenterFraction = double(center) / gray.width();
      evidence.gutterWidthFraction = double(bestLength) / gray.width();
    }
  }
  // Keep the legacy AUTO fallback conservative: below 1.25, gutter evidence
  // is recorded for the affirmative-Vision policy but does not independently
  // make the page spread-shaped for the line / whitespace splitters.
  evidence.isSpread =
      evidence.aspectRatio >= kGutterMinimumAspect
      && evidence.hasFullHeightGutter;
  evidence.reason =
      evidence.isSpread
          ? QStringLiteral("spread_geometry_two_textured_sides_full_height_gutter")
          : evidence.aspectRatio < kGutterMinimumAspect
              ? (evidence.hasFullHeightGutter
                     ? QStringLiteral("spread_geometry_near_square_full_height_gutter")
                     : QStringLiteral("spread_geometry_single_page_aspect"))
              : QStringLiteral("spread_geometry_no_full_height_gutter");
  return evidence;
}
