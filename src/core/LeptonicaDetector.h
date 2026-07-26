// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_LEPTONICADETECTOR_H_
#define SCANTAILOR_CORE_LEPTONICADETECTOR_H_

#include <QSize>
#include <QRect>
#include <QString>
#include <QVector>

#include <array>

class QImage;
class TaskStatus;

/**
 * Document color type detection using Leptonica library.
 * Uses sampled chroma, normalized grayscale histograms, and localized
 * continuous-tone evidence to distinguish document output modes.
 */
class LeptonicaDetector {
 public:
  // Version 3 invalidates automatic verdicts once so MIXED pages acquire the
  // region geometry now consumed by Output.
  static constexpr int DETECTOR_SCHEMA_VERSION = 3;

  enum class ColorType { BlackWhite, Grayscale, Mixed, Color };

  struct AnalysisScalePolicy {
    int targetLongEdge = 0;
    int minimumShortEdge = 300;

    static AnalysisScalePolicy productionDefault();
  };

  struct CellEvidence {
    int gridX = 0;
    int gridY = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float midtoneRatio = 0.0f;
    bool sampled = false;
    bool aboveThreshold = false;
  };

  struct ToneTileEvidence {
    int gridX = 0;
    int gridY = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float broadMidtoneRatio = 0.0f;
    float histogramEntropy = 0.0f;
    float blurredEntropy = 0.0f;
    float edgeDensity = 0.0f;
    float edgeHaloMidtoneRatio = 0.0f;
    float lowFrequencyVariance = 0.0f;
    int occupiedBins = 0;
    bool continuousTone = false;
    bool lineArtRejected = false;
  };

  struct DetectionEvidence {
    enum class CastPath {
      None,
      NoSignificantCast,
      WhiteBalanceRecheck,
      SelectiveNeutralizationRecheck
    };

    QSize analysisSize;
    QSize sourceSize;
    int detectorSchemaVersion = DETECTOR_SCHEMA_VERSION;
    int requestedLongEdgeCap = 0;
    int minimumShortEdge = 300;
    bool scaleApplied = false;
    bool grayscale = true;
    float analyzedFraction = 0.0f;
    float colorFraction = 0.0f;
    float preCastColorFraction = 0.0f;
    float darkRatio = 0.0f;
    float midtoneRatio = 0.0f;
    float lightRatio = 0.0f;
    float cellThreshold = 0.0f;
    std::array<CellEvidence, 16> interiorCells;
    int highMidtoneCellCount = 0;
    std::array<ToneTileEvidence, 81> toneTiles;
    int continuousToneTileCount = 0;
    int continuousToneRegionCount = 0;
    int largestRegionTileCount = 0;
    float continuousToneCoverage = 0.0f;
    float largestRegionCoverage = 0.0f;
    QRect largestRegionBounds;
    QVector<QRect> regionBounds;
    float documentPriorScore = 0.0f;
    bool documentPrior = false;
    bool widespreadTone = false;
    QString reason;
    ColorType preCastVerdict = ColorType::Grayscale;
    ColorType rawVerdict = ColorType::Grayscale;
    CastPath castPath = CastPath::None;
  };

  /** Pure inspectable classifier for the current evidence schema. */
  static ColorType classify(const DetectionEvidence& evidence);
  static QString classificationReason(const DetectionEvidence& evidence);

  /**
   * Detect the color type of an image.
   * Uses Leptonica's pixColorFraction() and channel analysis.
   *
   * @param image The image to analyze
   * @param midtoneThreshold Sensitivity adjustment for localized picture
   *        evidence (default 10)
   * @return The detected color type
   */
  static ColorType detect(const QImage& image,
                          int midtoneThreshold = 10,
                          DetectionEvidence* evidence = nullptr,
                          const AnalysisScalePolicy& scalePolicy = AnalysisScalePolicy::productionDefault(),
                          const TaskStatus* status = nullptr);

  /**
   * Detect the color type, compensating for an overall paper tint.
   *
   * If plain detection classifies the image as Color but the background
   * (brightest-quartile estimate) carries a significant cast - aged/toned
   * paper - the image is neutralized against that background color and
   * detection re-runs. The page is only classified Color if the color
   * fraction survives neutralization (a genuine color photo does; uniform
   * tan paper with black text does not).
   *
   * The extra pass only runs for Color-classified images whose background
   * has a cast, so already-neutralized or truly neutral pages pay nothing.
   *
   * @param image The image to analyze
   * @param midtoneThreshold Sensitivity adjustment for localized picture
   *        evidence (default 10)
   * @return The detected color type
   */
  static ColorType detectWithCastCompensation(const QImage& image,
                                              int midtoneThreshold = 10,
                                              DetectionEvidence* evidence = nullptr,
                                              const AnalysisScalePolicy& scalePolicy = AnalysisScalePolicy::productionDefault(),
                                              const TaskStatus* status = nullptr);

  /**
   * Detect from file path (loads downsampled for efficiency).
   */
  static ColorType detectFromFile(const QString& imagePath);

  /**
   * Get string representation of color type for logging.
   */
  static const char* colorTypeToString(ColorType type);

  static const char* castPathToString(DetectionEvidence::CastPath path);
};

#endif  // SCANTAILOR_CORE_LEPTONICADETECTOR_H_
