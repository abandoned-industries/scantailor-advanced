// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license.

#ifndef SCANTAILOR_CORE_PLATEPRIOR_H_
#define SCANTAILOR_CORE_PLATEPRIOR_H_

#include <QString>
#include <QRectF>
#include <QSize>
#include <QVector>

class QImage;

class PlatePrior {
 public:
  struct Evidence {
    bool isPlate = false;
    double entropy = 0.0;
    double tonalTileFraction = 0.0;
    double midtoneFraction = 0.0;
    QString reason;
  };

  struct CaptionEvidence {
    bool isCaptionScale = false;
    int regionCount = 0;
    int peripheralRegionCount = 0;
    double totalAreaFraction = 0.0;
    double interiorAreaFraction = 0.0;
    QString reason;
  };

  struct SpreadEvidence {
    bool isSpread = false;
    bool hasFullHeightGutter = false;
    double aspectRatio = 0.0;
    double gutterCenterFraction = 0.0;
    double gutterWidthFraction = 0.0;
    QString reason;
  };

  /**
   * Cheap, bounded continuous-tone prior for geometry stages.
   *
   * The image is sampled at no more than 512 pixels on its long edge.
   * Text pages remain below the global-entropy and spatial tonal-coverage
   * gates; photographs contain varied midtones across many tiles.
   */
  static Evidence analyze(const QImage& image);

  /**
   * Accept a bounded amount of Vision text on a plate. Real captions in the
   * geometry census occupy a top/bottom band; small false OCR hits inside the
   * photograph are tolerated separately and remain capped.
   */
  static CaptionEvidence analyzeCaptionText(
      const QVector<QRectF>& textRegions, const QSize& imageSize);

  /**
   * Decide whether plate geometry is plausibly a scanned two-page spread.
   * Very wide pages qualify directly. Moderately wide pages require a
   * vertically uniform central gutter with textured material on both sides.
   * Gutter evidence is also measured below the moderate-aspect threshold for
   * callers that apply it independently, without changing isSpread there.
   */
  static SpreadEvidence analyzeSpreadGeometry(const QImage& image);
};

#endif  // SCANTAILOR_CORE_PLATEPRIOR_H_
