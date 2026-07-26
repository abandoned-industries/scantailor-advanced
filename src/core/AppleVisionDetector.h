// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_APPLEVISIONDETECTOR_H_
#define SCANTAILOR_CORE_APPLEVISIONDETECTOR_H_

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

/**
 * Apple Vision Framework integration for page split detection and OCR.
 * Uses macOS Vision APIs for text recognition to detect two-page spreads
 * and perform OCR for searchable PDF generation.
 * Falls back to disabled on non-Apple platforms.
 */
class AppleVisionDetector {
 public:
  /**
   * Detected text region with confidence score.
   * Used internally for page split detection.
   */
  struct TextRegion {
    QRectF bounds;       // Bounding rectangle in image coordinates
    float confidence;    // Detection confidence (0.0 - 1.0)
    QString text;        // Recognized text (if available)
  };

  /**
   * OCR configuration for text recognition.
   */
  struct OcrConfig {
    QString languageCode;              // Empty = auto-detect, or specific language code (e.g., "en-US")
    bool useAccurateRecognition = true;  // Accurate vs Fast recognition level
    bool usesLanguageCorrection = true;  // Apply language model corrections
  };

  /** OCR result for one Vision text observation (normally a line/block). */
  struct OcrWordResult {
    QString text;
    QRectF bounds;       // In image coordinates
    float confidence;
  };

  struct RectangleResult {
    QRectF bounds;       // Axis-aligned bounds in image coordinates.
    float confidence;    // Vision observation confidence.
  };

  /**
   * Page split detection result.
   */
  struct PageSplitResult {
    bool shouldSplit;        // True if a two-page layout was detected
    double splitLineX;       // X coordinate of suggested split line (normalized 0.0-1.0)
    float confidence;        // Detection confidence (0.0 - 1.0)
    int leftTextRegions;     // Number of text regions on left side
    int rightTextRegions;    // Number of text regions on right side
    bool hasAnyPageNumber;   // True when a page number was detected on either side
    bool hasPageNumbers;     // True when page numbers were detected on both sides
    double rightmostLeftTextX;  // Normalized X of rightmost text region in left zone (0.0-1.0), 0 if none
    QVector<TextRegion> textRegions;  // Same observations used for this decision
  };

  /**
   * Check if Apple Vision is available on this platform.
   */
  static bool isAvailable();

  /**
   * Detect text regions in an image.
   * @param image The image to analyze
   * @return Vector of detected text regions
   */
  static QVector<TextRegion> detectTextRegions(const QImage& image);

  /**
   * Detect multiple near-axis-aligned printed rectangles.  This is deliberately
   * only a proposal source; callers must validate the interior semantics.
   */
  static QVector<RectangleResult> detectRectangles(const QImage& image);

  /**
   * Detect if an image contains two pages (like a book spread) and suggest split location.
   * Uses text region clustering to find natural page boundaries.
   * @param image The image to analyze
   * @return Page split detection result
   */
  static PageSplitResult detectPageSplit(const QImage& image);

  /**
   * Detect page split from file path (with automatic downscaling for performance).
   * @param imagePath Path to the image file
   * @return Page split detection result
   */
  static PageSplitResult detectPageSplitFromFile(const QString& imagePath);

  /**
   * Perform OCR with line/block results for the PDF text layer.
   * @param image The image to analyze
   * @param config OCR configuration (language, accuracy level)
   * @param success Optional result distinguishing a valid blank page from a request failure
   * @return Vector of text-block results with bounding boxes
   */
  static QVector<OcrWordResult> performOcr(
      const QImage& image, const OcrConfig& config, bool* success = nullptr);

  /**
   * Get list of supported language codes for OCR.
   * @return List of language codes (e.g., "en-US", "fr-FR")
   */
  static QStringList supportedOcrLanguages();
};

#endif  // SCANTAILOR_CORE_APPLEVISIONDETECTOR_H_
