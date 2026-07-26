// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "LeptonicaDetector.h"

#include <QImage>
#include <QImageReader>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <queue>

#include <leptonica/allheaders.h>

#include "WhiteBalance.h"
#include "TaskStatus.h"

namespace {

struct PixDeleter {
  void operator()(PIX* pix) const {
    pixDestroy(&pix);
  }
};

struct NumaDeleter {
  void operator()(NUMA* numa) const {
    numaDestroy(&numa);
  }
};

using PixPtr = std::unique_ptr<PIX, PixDeleter>;
using NumaPtr = std::unique_ptr<NUMA, NumaDeleter>;

/**
 * Convert QImage to Leptonica PIX format.
 * Caller must call pixDestroy() on the returned PIX.
 */
PIX* qImageToPix(const QImage& qimg) {
  QImage img = qimg;

  // Convert to 32-bit ARGB if needed
  if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB32) {
    img = img.convertToFormat(QImage::Format_ARGB32);
  }

  const int w = img.width();
  const int h = img.height();

  PIX* pix = pixCreate(w, h, 32);
  if (!pix) return nullptr;

  l_uint32* pixData = pixGetData(pix);
  const int wpl = pixGetWpl(pix);

  for (int y = 0; y < h; y++) {
    const QRgb* scanline = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    l_uint32* line = pixData + y * wpl;
    for (int x = 0; x < w; x++) {
      QRgb pixel = scanline[x];
      // Leptonica uses RGBA format
      composeRGBAPixel(qRed(pixel), qGreen(pixel), qBlue(pixel), 255, line + x);
    }
  }

  return pix;
}

/**
 * Remove a dominant low-saturation paper cast without desaturating unrelated
 * colors. A single RGB gain neutralizes one sampled paper brightness, but real
 * book scans have shadows and scanner-response gradients whose chroma remains
 * above pixColorFraction's threshold after that correction.
 *
 * Pixels are neutralized only when their chroma points in nearly the same
 * direction as the sampled paper cast and is no more than three times as
 * strong. Saturated inks and photo colors therefore remain available to the
 * detector.
 */
QImage neutralizeDominantPaperCast(const QImage& image, const QColor& paperColor) {
  if (image.isNull() || !paperColor.isValid()) {
    return image;
  }

  const int paperAverage = (paperColor.red() + paperColor.green() + paperColor.blue()) / 3;
  const int paperCastR = paperColor.red() - paperAverage;
  const int paperCastG = paperColor.green() - paperAverage;
  const int paperCastB = paperColor.blue() - paperAverage;
  const qint64 paperNorm2 = static_cast<qint64>(paperCastR) * paperCastR
                           + static_cast<qint64>(paperCastG) * paperCastG
                           + static_cast<qint64>(paperCastB) * paperCastB;
  if (paperNorm2 < 25) {
    return image;
  }

  QImage result = image.convertToFormat(QImage::Format_RGB32);
  for (int y = 0; y < result.height(); ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < result.width(); ++x) {
      const int red = qRed(line[x]);
      const int green = qGreen(line[x]);
      const int blue = qBlue(line[x]);
      const int average = (red + green + blue) / 3;
      const int castR = red - average;
      const int castG = green - average;
      const int castB = blue - average;
      const qint64 norm2 = static_cast<qint64>(castR) * castR
                           + static_cast<qint64>(castG) * castG
                           + static_cast<qint64>(castB) * castB;
      if (norm2 == 0 || norm2 > paperNorm2 * 9) {
        continue;
      }

      const qint64 dot = static_cast<qint64>(castR) * paperCastR
                         + static_cast<qint64>(castG) * paperCastG
                         + static_cast<qint64>(castB) * paperCastB;
      // cos(angle) >= 0.90, expressed without a square root.
      if (dot > 0 && dot * dot * 100 >= norm2 * paperNorm2 * 81) {
        line[x] = qRgb(average, average, average);
      }
    }
  }
  return result;
}

/**
 * Check if a region of a brightness-normalized grayscale image has high
 * midtone concentration.
 * Used to detect embedded images/illustrations in otherwise B&W pages.
 * Returns the number of interior grid cells above the configured midtone
 * concentration threshold.
 * Skips edge cells which often contain page gutters, binding shadows, or margins.
 */
int collectInteriorCellEvidence(PIX* gray,
                                int midtoneThreshold,
                                LeptonicaDetector::DetectionEvidence* evidence) {
  const int w = pixGetWidth(gray);
  const int h = pixGetHeight(gray);

  // Use 6x6 grid (36 cells) for region analysis
  const int gridSize = 6;
  const int cellW = w / gridSize;
  const int cellH = h / gridSize;
  const float cellThreshold = std::clamp(0.60f - midtoneThreshold / 100.0f, 0.35f, 0.60f);

  if (evidence) {
    evidence->cellThreshold = cellThreshold;
    int index = 0;
    for (int gy = 1; gy < gridSize - 1; ++gy) {
      for (int gx = 1; gx < gridSize - 1; ++gx) {
        auto& cell = evidence->interiorCells[index++];
        cell.gridX = gx;
        cell.gridY = gy;
        cell.x = gx * cellW;
        cell.y = gy * cellH;
        cell.width = cellW;
        cell.height = cellH;
      }
    }
  }

  // Skip if cells would be too small
  if (cellW < 50 || cellH < 50) {
    return 0;
  }

  l_uint32* pixData = pixGetData(gray);
  const int wpl = pixGetWpl(gray);

  // Check only interior grid cells (skip edges which often have margins/gutters)
  // With 6x6 grid, check cells [1,1] through [4,4] (16 interior cells)
  int highMidtoneCells = 0;
  int cellIndex = 0;
  for (int gy = 1; gy < gridSize - 1; gy++) {
    for (int gx = 1; gx < gridSize - 1; gx++) {
      int startX = gx * cellW;
      int startY = gy * cellH;
      int endX = (gx == gridSize - 1) ? w : startX + cellW;
      int endY = (gy == gridSize - 1) ? h : startY + cellH;

      int cellMidtones = 0;
      int cellTotal = 0;

      // Sample every 4th pixel for speed
      for (int y = startY; y < endY; y += 4) {
        l_uint32* line = pixData + y * wpl;
        for (int x = startX; x < endX; x += 4) {
          // For 8-bit grayscale, get pixel value
          l_uint32 val = GET_DATA_BYTE(line, x);
          cellTotal++;
          // Midtone range: 60-195
          if (val > 60 && val < 195) {
            cellMidtones++;
          }
        }
      }

      if (cellTotal > 0) {
        float cellMidtoneRatio = (float)cellMidtones / cellTotal;
        if (evidence) {
          auto& cell = evidence->interiorCells[cellIndex];
          cell.midtoneRatio = cellMidtoneRatio;
          cell.sampled = true;
          cell.aboveThreshold = cellMidtoneRatio > cellThreshold;
        }
        // The 150-DPI census of all 392 pages in the toned-paper regression
        // book topped out at 41.8% for a dense text cell after brightness
        // normalization. The continuous-tone fixture exceeds 50%, leaving
        // an evidence-backed gap instead of counting a page-wide paper slope.
        if (cellMidtoneRatio > cellThreshold) {
          fprintf(stderr, "  Region [%d,%d]: %.1f%% midtones -> embedded image detected\n",
                  gx, gy, cellMidtoneRatio * 100);
          ++highMidtoneCells;
        }
      }
      ++cellIndex;
    }
  }
  if (evidence) {
    evidence->highMidtoneCellCount = highMidtoneCells;
  }
  return highMidtoneCells;
}

float normalizedEntropy(const std::array<int, 16>& histogram, const int total) {
  if (total <= 0) return 0.0f;
  float entropy = 0.0f;
  for (const int count : histogram) {
    if (count == 0) continue;
    const float probability = static_cast<float>(count) / total;
    entropy -= probability * std::log2(probability);
  }
  return entropy / 4.0f;
}

/**
 * Measure overlapping 20%-page tiles on a 10%-page stride.  The 4% inset
 * suppresses scanner margins and binding gutters explicitly, while tiles
 * still reach every content edge.  Candidate tiles are joined in the 9x9
 * overlap graph so classification can use coverage and topology rather than
 * one position-sensitive cell.
 */
void collectConnectedToneEvidence(PIX* gray,
                                  LeptonicaDetector::DetectionEvidence* evidence) {
  const int width = pixGetWidth(gray);
  const int height = pixGetHeight(gray);
  if (width < 100 || height < 100) return;

  const int marginX = std::max(2, width * 4 / 100);
  const int marginY = std::max(2, height * 4 / 100);
  const int contentWidth = width - 2 * marginX;
  const int contentHeight = height - 2 * marginY;
  const int tileWidth = std::max(40, contentWidth / 5);
  const int tileHeight = std::max(40, contentHeight / 5);
  const int strideX = std::max(1, (contentWidth - tileWidth) / 8);
  const int strideY = std::max(1, (contentHeight - tileHeight) / 8);
  l_uint32* data = pixGetData(gray);
  const int wpl = pixGetWpl(gray);

  for (int gy = 0; gy < 9; ++gy) {
    for (int gx = 0; gx < 9; ++gx) {
      auto& tile = evidence->toneTiles[gy * 9 + gx];
      tile.gridX = gx;
      tile.gridY = gy;
      tile.x = marginX + gx * strideX;
      tile.y = marginY + gy * strideY;
      tile.width = std::min(tileWidth, width - marginX - tile.x);
      tile.height = std::min(tileHeight, height - marginY - tile.y);

      std::array<int, 16> histogram{};
      std::array<int, 16> blurredHistogram{};
      int total = 0;
      int broadMidtones = 0;
      int edges = 0;
      int edgeHaloMidtones = 0;
      double blockSum = 0.0;
      double blockSumSquares = 0.0;
      int blockCount = 0;
      const int sampleStep = 2;
      const int blurStep = 8;

      for (int y = tile.y + 1; y < tile.y + tile.height - 1; y += sampleStep) {
        l_uint32* line = data + y * wpl;
        l_uint32* nextLine = data + (y + 1) * wpl;
        for (int x = tile.x + 1; x < tile.x + tile.width - 1; x += sampleStep) {
          const int value = GET_DATA_BYTE(line, x);
          const int gradient = std::abs(value - GET_DATA_BYTE(line, x + 1))
                               + std::abs(value - GET_DATA_BYTE(nextLine, x));
          ++histogram[std::min(15, value / 16)];
          ++total;
          if (value > 40 && value < 216) {
            ++broadMidtones;
            if (gradient > 55) ++edgeHaloMidtones;
          }
          if (gradient > 70) ++edges;
        }
      }

      for (int by = tile.y; by + blurStep <= tile.y + tile.height; by += blurStep) {
        for (int bx = tile.x; bx + blurStep <= tile.x + tile.width; bx += blurStep) {
          int sum = 0;
          for (int yy = 0; yy < blurStep; yy += 2) {
            l_uint32* line = data + (by + yy) * wpl;
            for (int xx = 0; xx < blurStep; xx += 2) {
              sum += GET_DATA_BYTE(line, bx + xx);
            }
          }
          const int average = sum / 16;
          ++blurredHistogram[std::min(15, average / 16)];
          blockSum += average;
          blockSumSquares += average * average;
          ++blockCount;
        }
      }

      tile.broadMidtoneRatio = total ? static_cast<float>(broadMidtones) / total : 0.0f;
      tile.histogramEntropy = normalizedEntropy(histogram, total);
      tile.blurredEntropy = normalizedEntropy(blurredHistogram, blockCount);
      tile.edgeDensity = total ? static_cast<float>(edges) / total : 0.0f;
      tile.edgeHaloMidtoneRatio =
          broadMidtones ? static_cast<float>(edgeHaloMidtones) / broadMidtones : 0.0f;
      for (const int count : blurredHistogram) {
        if (count >= std::max(2, blockCount / 200)) ++tile.occupiedBins;
      }
      if (blockCount) {
        const double mean = blockSum / blockCount;
        tile.lowFrequencyVariance =
            static_cast<float>(std::max(0.0, blockSumSquares / blockCount - mean * mean)
                               / (255.0 * 255.0));
      }

      // Photos retain a broad distribution after an 8px box integration.
      // Line art instead has few blurred bins or concentrates its intermediate
      // samples in high-contrast edge halos.  Very flat fills also fail.
      tile.lineArtRejected =
          tile.occupiedBins < 7 || tile.blurredEntropy < 0.55f
          || tile.lowFrequencyVariance < 0.012f
          || tile.broadMidtoneRatio > 0.76f
          || (tile.edgeDensity > 0.18f && tile.edgeHaloMidtoneRatio > 0.30f);
      tile.continuousTone =
          tile.broadMidtoneRatio >= 0.05f
          && tile.histogramEntropy >= 0.62f
          && tile.blurredEntropy >= 0.55f
          && tile.occupiedBins >= 7
          && !tile.lineArtRejected;
      if (tile.continuousTone) ++evidence->continuousToneTileCount;
    }
  }

  std::array<bool, 81> visited{};
  std::array<bool, 100> covered{};
  for (int start = 0; start < 81; ++start) {
    if (visited[start] || !evidence->toneTiles[start].continuousTone) continue;
    ++evidence->continuousToneRegionCount;
    int regionTiles = 0;
    std::array<bool, 100> regionCovered{};
    int regionLeft = width;
    int regionTop = height;
    int regionRight = 0;
    int regionBottom = 0;
    std::queue<int> pending;
    pending.push(start);
    visited[start] = true;
    while (!pending.empty()) {
      const int index = pending.front();
      pending.pop();
      ++regionTiles;
      const auto& tile = evidence->toneTiles[index];
      regionLeft = std::min(regionLeft, tile.x);
      regionTop = std::min(regionTop, tile.y);
      regionRight = std::max(regionRight, tile.x + tile.width);
      regionBottom = std::max(regionBottom, tile.y + tile.height);
      const int x = index % 9;
      const int y = index / 9;
      for (int yy = y; yy <= y + 1; ++yy) {
        for (int xx = x; xx <= x + 1; ++xx) {
          regionCovered[yy * 10 + xx] = true;
          covered[yy * 10 + xx] = true;
        }
      }
      constexpr int dx[] = {-1, 1, 0, 0};
      constexpr int dy[] = {0, 0, -1, 1};
      for (int direction = 0; direction < 4; ++direction) {
        const int nx = x + dx[direction];
        const int ny = y + dy[direction];
        if (nx < 0 || nx >= 9 || ny < 0 || ny >= 9) continue;
        const int neighbor = ny * 9 + nx;
        if (!visited[neighbor] && evidence->toneTiles[neighbor].continuousTone) {
          visited[neighbor] = true;
          pending.push(neighbor);
        }
      }
    }
    int regionCells = 0;
    for (const bool cell : regionCovered) regionCells += cell ? 1 : 0;
    evidence->regionBounds.push_back(
        QRect(regionLeft, regionTop, regionRight - regionLeft, regionBottom - regionTop));
    if (regionTiles > evidence->largestRegionTileCount) {
      evidence->largestRegionTileCount = regionTiles;
      evidence->largestRegionCoverage = regionCells / 100.0f;
      evidence->largestRegionBounds =
          QRect(regionLeft, regionTop, regionRight - regionLeft, regionBottom - regionTop);
    }
  }
  int coveredCells = 0;
  for (const bool cell : covered) coveredCells += cell ? 1 : 0;
  evidence->continuousToneCoverage = coveredCells / 100.0f;
}

/**
 * Collect normalized grayscale and spatial tone measurements.  This function
 * deliberately makes no verdict.
 */
void collectGrayscaleToneEvidence(
    PIX* pix,
    int midtoneThreshold,
    LeptonicaDetector::DetectionEvidence* evidence,
    const TaskStatus* status) {
  if (status) status->throwIfCancelled();
  // Normalize low-frequency brightness before measuring tone. This flattens
  // shadows and paper gradients while retaining local photograph detail.
  PixPtr gray(pixConvertRGBToGray(pix, 0.0, 0.0, 0.0));
  if (!gray) return;
  PIX* normalized = pixBackgroundNormSimple(gray.get(), nullptr, nullptr);
  if (normalized) {
    gray.reset(normalized);
  }
  if (status) status->throwIfCancelled();

  // Get histogram
  NumaPtr histo(pixGetGrayHistogram(gray.get(), 1));
  if (!histo) {
    return;
  }

  l_int32 n = numaGetCount(histo.get());
  l_float32 total = 0;
  l_float32 darks = 0;      // 0-79: black zone (text + dark edges)
  l_float32 midtones = 0;   // 80-129: true midtone zone (photos, illustrations)
  l_float32 lights = 0;     // 130-255: white zone (paper, including yellowed/grayish)

  for (int i = 0; i < n; i++) {
    l_float32 val;
    numaGetFValue(histo.get(), i, &val);
    total += val;
    if (i <= 79) {
      darks += val;
    } else if (i >= 130) {
      lights += val;  // Lowered from 150 to include grayish/yellowed paper
    } else {
      midtones += val;
    }
  }

  if (total == 0) {
    return;
  }

  float midRatio = midtones / total;
  float darkRatio = darks / total;
  float lightRatio = lights / total;
  evidence->darkRatio = darkRatio;
  evidence->midtoneRatio = midRatio;
  evidence->lightRatio = lightRatio;
  collectInteriorCellEvidence(gray.get(), midtoneThreshold, evidence);
  collectConnectedToneEvidence(gray.get(), evidence);
  if (status) status->throwIfCancelled();
}

/**
 * Check if image is grayscale (R≈G≈B for all pixels).
 */
bool isGrayscale(PIX* pix, float* analyzedFraction, float* colorFraction) {
  l_float32 pixfract = 0.0f;
  l_float32 colorfract = 0.0f;

  // pixColorFraction analyzes what fraction of pixels have color
  // darkthresh: ignore pixels darker than this (avoid noise in shadows)
  // lightthresh: ignore pixels lighter than this (avoid noise in highlights)
  // diffthresh: minimum R-G, R-B, G-B difference to count as "color"
  // factor: subsampling factor for speed
  l_int32 result = pixColorFraction(pix,
                                     10,    // darkthresh - lowered from 20 to include dark photos
                                     240,   // lightthresh - raised from 235 to include more highlights
                                     50,    // diffthresh - raised from 35 to tolerate heavily yellowed old paper
                                     4,     // factor (subsample for speed)
                                     &pixfract,   // fraction of pixels analyzed
                                     &colorfract); // fraction of those that are color

  if (result != 0) {
    // Error - assume grayscale
    if (analyzedFraction) *analyzedFraction = 0.0f;
    if (colorFraction) *colorFraction = 0.0f;
    return true;
  }

  if (analyzedFraction) *analyzedFraction = pixfract;
  if (colorFraction) *colorFraction = colorfract;

  // If less than 3% of analyzed pixels have significant color, it's grayscale
  // Lowered from 10% to catch photos with muted colors and dark backgrounds
  return colorfract < 0.03f;
}

LeptonicaDetector::ColorType detectPrepared(
    const QImage& image,
    int midtoneThreshold,
    LeptonicaDetector::DetectionEvidence* evidence,
    const TaskStatus* status) {
  using ColorType = LeptonicaDetector::ColorType;
  LeptonicaDetector::DetectionEvidence localEvidence;
  LeptonicaDetector::DetectionEvidence* measured = evidence ? evidence : &localEvidence;
  *measured = LeptonicaDetector::DetectionEvidence();
  measured->analysisSize = image.size();
  if (image.isNull()) {
    return ColorType::Grayscale;
  }

  if (status) status->throwIfCancelled();
  PixPtr pix(qImageToPix(image));
  if (!pix) {
    return ColorType::Grayscale;
  }
  if (status) status->throwIfCancelled();

  float analyzedFraction = 0.0f;
  float colorFraction = 0.0f;
  bool grayscale = isGrayscale(pix.get(), &analyzedFraction, &colorFraction);
  measured->analyzedFraction = analyzedFraction;
  measured->colorFraction = colorFraction;
  measured->grayscale = grayscale;

  // Evidence is now the primary artifact. The compatibility verdict is
  // derived only after every measurement has been recorded.
  collectGrayscaleToneEvidence(pix.get(), midtoneThreshold, measured, status);
  const bool bimodalDocument =
      measured->darkRatio > 0.10f && measured->lightRatio > 0.30f;
  measured->documentPrior =
      measured->midtoneRatio < 0.45f && (bimodalDocument || measured->lightRatio > 0.70f);
  measured->documentPriorScore =
      std::clamp((measured->darkRatio - 0.06f) * 2.5f
                     + (measured->lightRatio - 0.30f),
                 0.0f,
                 1.0f);
  measured->widespreadTone =
      measured->midtoneRatio >= 0.10f
      || measured->continuousToneCoverage >= 0.70f
      || measured->largestRegionCoverage >= 0.70f
      || measured->continuousToneTileCount >= 45;
  const ColorType result = LeptonicaDetector::classify(*measured);
  measured->reason = LeptonicaDetector::classificationReason(*measured);
  if (result == ColorType::Color) {
    fprintf(stderr, "LeptonicaDetector: COLOR (%.1f%% color pixels)\n", colorFraction * 100);
  } else if (result == ColorType::BlackWhite) {
    fprintf(stderr, "LeptonicaDetector: B&W (%.1f%% midtones)\n", measured->midtoneRatio * 100);
  } else if (result == ColorType::Mixed) {
    fprintf(stderr, "LeptonicaDetector: MIXED (%.1f%% midtones)\n", measured->midtoneRatio * 100);
  } else {
    fprintf(stderr, "LeptonicaDetector: GRAYSCALE (%.1f%% color, %.1f%% midtones)\n",
            colorFraction * 100, measured->midtoneRatio * 100);
  }

  measured->preCastColorFraction = colorFraction;
  measured->preCastVerdict = result;
  measured->rawVerdict = result;
  return result;
}

QImage analysisImageForPolicy(const QImage& source,
                              const LeptonicaDetector::AnalysisScalePolicy& policy,
                              bool* scaled) {
  *scaled = false;
  if (source.isNull() || policy.targetLongEdge <= 0) return source;
  const int longEdge = std::max(source.width(), source.height());
  const int shortEdge = std::min(source.width(), source.height());
  if (longEdge <= policy.targetLongEdge || shortEdge <= 0) return source;
  const qreal capScale = static_cast<qreal>(policy.targetLongEdge) / longEdge;
  const qreal floorScale = static_cast<qreal>(policy.minimumShortEdge) / shortEdge;
  const qreal scale = std::min<qreal>(1.0, std::max(capScale, floorScale));
  if (scale >= 1.0) return source;
  *scaled = true;
  return source.scaled(qRound(source.width() * scale),
                       qRound(source.height() * scale),
                       Qt::IgnoreAspectRatio,
                       Qt::SmoothTransformation);
}

}  // namespace

LeptonicaDetector::AnalysisScalePolicy LeptonicaDetector::AnalysisScalePolicy::productionDefault() {
  AnalysisScalePolicy policy;
  policy.targetLongEdge = 1200;
  bool ok = false;
  const int envCap = qEnvironmentVariableIntValue("SCANTAILOR_DETECT_MAX_DIM", &ok);
  if (ok && envCap > 0) {
    policy.targetLongEdge = envCap;
  }
  return policy;
}

LeptonicaDetector::ColorType LeptonicaDetector::classify(const DetectionEvidence& evidence) {
  if (!evidence.grayscale) return ColorType::Color;

  const bool bimodalDocument =
      evidence.darkRatio > 0.10f && evidence.lightRatio > 0.30f;
  const bool lightDocument = evidence.lightRatio > 0.70f;
  const bool documentPrior = evidence.midtoneRatio < 0.45f
                             && (bimodalDocument || lightDocument);
  const bool widespreadTone =
      evidence.midtoneRatio >= 0.10f
      || evidence.continuousToneCoverage >= 0.70f
      || evidence.largestRegionCoverage >= 0.70f
      || evidence.continuousToneTileCount >= 45;
  if (!documentPrior) return ColorType::Grayscale;
  if (widespreadTone) return ColorType::Grayscale;
  if (evidence.largestRegionTileCount >= 2
      && (evidence.darkRatio >= 0.08f || evidence.largestRegionTileCount >= 3)
      && evidence.largestRegionCoverage >= 0.06f) {
    return ColorType::Mixed;
  }
  return ColorType::BlackWhite;
}

QString LeptonicaDetector::classificationReason(const DetectionEvidence& evidence) {
  if (!evidence.grayscale) return QStringLiteral("global_chroma");
  const bool documentPrior =
      evidence.midtoneRatio < 0.45f
      && ((evidence.darkRatio > 0.10f && evidence.lightRatio > 0.30f)
          || evidence.lightRatio > 0.70f);
  const bool widespreadTone =
      evidence.midtoneRatio >= 0.10f
      || evidence.continuousToneCoverage >= 0.70f
      || evidence.largestRegionCoverage >= 0.70f
      || evidence.continuousToneTileCount >= 45;
  if (!documentPrior) return QStringLiteral("document_prior_absent");
  if (widespreadTone) return QStringLiteral("widespread_continuous_tone");
  if (evidence.largestRegionTileCount >= 2
      && (evidence.darkRatio >= 0.08f || evidence.largestRegionTileCount >= 3)
      && evidence.largestRegionCoverage >= 0.06f) {
    return QStringLiteral("localized_continuous_tone_with_document");
  }
  if (evidence.continuousToneTileCount > 0) {
    return QStringLiteral("isolated_tone_below_region_minimum");
  }
  return QStringLiteral("document_without_continuous_tone");
}

LeptonicaDetector::ColorType LeptonicaDetector::detect(const QImage& image,
                                                       int midtoneThreshold,
                                                       DetectionEvidence* evidence,
                                                       const AnalysisScalePolicy& scalePolicy,
                                                       const TaskStatus* status) {
  bool scaled = false;
  const QImage analysisImage = analysisImageForPolicy(image, scalePolicy, &scaled);
  DetectionEvidence localEvidence;
  DetectionEvidence* measured = evidence ? evidence : &localEvidence;
  const ColorType result = detectPrepared(analysisImage, midtoneThreshold, measured, status);
  measured->sourceSize = image.size();
  measured->requestedLongEdgeCap = scalePolicy.targetLongEdge;
  measured->minimumShortEdge = scalePolicy.minimumShortEdge;
  measured->scaleApplied = scaled;
  return result;
}

LeptonicaDetector::ColorType LeptonicaDetector::detectWithCastCompensation(const QImage& image,
                                                                           const int midtoneThreshold,
                                                                           DetectionEvidence* evidence,
                                                                           const AnalysisScalePolicy& scalePolicy,
                                                                           const TaskStatus* status) {
  bool scaled = false;
  const QImage analysisImage = analysisImageForPolicy(image, scalePolicy, &scaled);
  DetectionEvidence initialEvidence;
  ColorType result = detectPrepared(analysisImage, midtoneThreshold, &initialEvidence, status);
  initialEvidence.sourceSize = image.size();
  initialEvidence.requestedLongEdgeCap = scalePolicy.targetLongEdge;
  initialEvidence.minimumShortEdge = scalePolicy.minimumShortEdge;
  initialEvidence.scaleApplied = scaled;
  if (result != ColorType::Color) {
    if (evidence) {
      *evidence = initialEvidence;
    }
    return result;
  }

  const QColor background = WhiteBalance::estimateBackgroundColor(analysisImage);
  if (status) status->throwIfCancelled();
  if (!background.isValid() || !WhiteBalance::hasSignificantCast(background)) {
    if (evidence) {
      *evidence = initialEvidence;
      evidence->castPath = DetectionEvidence::CastPath::NoSignificantCast;
    }
    return result;  // Neutral background - the color is genuine content
  }

  fprintf(stderr, "LeptonicaDetector: COLOR verdict with tinted background (%d,%d,%d), re-detecting neutralized\n",
          background.red(), background.green(), background.blue());
  const QImage neutralized = WhiteBalance::apply(analysisImage, background);
  if (status) status->throwIfCancelled();
  DetectionEvidence finalEvidence;
  ColorType recheck = detectPrepared(neutralized, midtoneThreshold, &finalEvidence, status);
  DetectionEvidence::CastPath castPath = DetectionEvidence::CastPath::WhiteBalanceRecheck;
  if (recheck == ColorType::Color) {
    float residualColorFraction = 1.0f;
    PixPtr neutralizedPix(qImageToPix(neutralized));
    if (neutralizedPix) {
      isGrayscale(neutralizedPix.get(), nullptr, &residualColorFraction);
    }

    // Only a modest residual is plausibly a brightness-varying paper cast.
    // A large surviving fraction is real page-level color even when its hues
    // are related to the paper (for example, a yellow library card).
    if (residualColorFraction < 0.20f) {
      const QImage selectivelyNeutralized = neutralizeDominantPaperCast(analysisImage, background);
      if (status) status->throwIfCancelled();
      recheck = detectPrepared(selectivelyNeutralized, midtoneThreshold, &finalEvidence, status);
      castPath = DetectionEvidence::CastPath::SelectiveNeutralizationRecheck;
    }
  }
  if (evidence) {
    *evidence = finalEvidence;
    evidence->analysisSize = analysisImage.size();
    evidence->sourceSize = image.size();
    evidence->requestedLongEdgeCap = scalePolicy.targetLongEdge;
    evidence->minimumShortEdge = scalePolicy.minimumShortEdge;
    evidence->scaleApplied = scaled;
    evidence->preCastColorFraction = initialEvidence.colorFraction;
    evidence->preCastVerdict = result;
    evidence->rawVerdict = recheck == ColorType::Color ? result : recheck;
    evidence->castPath = castPath;
  }
  if (recheck != ColorType::Color) {
    fprintf(stderr, "LeptonicaDetector: color did not survive tint neutralization -> %s\n",
            colorTypeToString(recheck));
    return recheck;
  }
  return result;
}

LeptonicaDetector::ColorType LeptonicaDetector::detectFromFile(const QString& imagePath) {
  QImageReader reader(imagePath);
  if (!reader.canRead()) {
    return ColorType::Grayscale;
  }

  // Downsample for speed
  const QSize originalSize = reader.size();
  const int maxDim = 1200;
  if (originalSize.width() > maxDim || originalSize.height() > maxDim) {
    const qreal scale = qMin(static_cast<qreal>(maxDim) / originalSize.width(),
                             static_cast<qreal>(maxDim) / originalSize.height());
    reader.setScaledSize(QSize(qRound(originalSize.width() * scale),
                               qRound(originalSize.height() * scale)));
  }

  const QImage image = reader.read();
  AnalysisScalePolicy policy;
  policy.targetLongEdge = 1200;
  return detect(image, 10, nullptr, policy);
}

const char* LeptonicaDetector::colorTypeToString(ColorType type) {
  switch (type) {
    case ColorType::BlackWhite: return "bw";
    case ColorType::Grayscale: return "grayscale";
    case ColorType::Mixed: return "mixed";
    case ColorType::Color: return "color";
  }
  return "unknown";
}

const char* LeptonicaDetector::castPathToString(DetectionEvidence::CastPath path) {
  switch (path) {
    case DetectionEvidence::CastPath::None: return "none";
    case DetectionEvidence::CastPath::NoSignificantCast: return "no_significant_cast";
    case DetectionEvidence::CastPath::WhiteBalanceRecheck: return "white_balance_recheck";
    case DetectionEvidence::CastPath::SelectiveNeutralizationRecheck:
      return "selective_neutralization_recheck";
  }
  return "unknown";
}
