// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FINALIZE_SETTINGS_H_
#define SCANTAILOR_FINALIZE_SETTINGS_H_

#include <QDomElement>
#include <QMutex>
#include <unordered_map>
#include <memory>

#include "NonCopyable.h"
#include "PageId.h"
#include "AutoColorModePolicy.h"

class AbstractRelinker;
class PageSequence;

namespace finalize {

// Color mode for a page
enum class ColorMode {
  BlackAndWhite,
  Grayscale,
  Mixed,
  Color
};

inline ColorMode applyAutoColorModePolicy(ColorMode detectedMode, AutoColorModePolicy policy) {
  if (policy == AutoColorModePolicy::ForceBlackAndWhite) {
    return ColorMode::BlackAndWhite;
  }
  if (policy == AutoColorModePolicy::NeverColor
      && (detectedMode == ColorMode::Color || detectedMode == ColorMode::Mixed)) {
    return ColorMode::Grayscale;
  }
  return detectedMode;
}

// Output image format
enum class OutputFormat {
  TIFF,
  PNG,
  JPEG
};

// TIFF compression options
enum class TiffCompression {
  LZW,
  Deflate
};

// Parameters stored per page
class Params {
 public:
  Params();
  explicit Params(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  ColorMode colorMode() const { return m_colorMode; }
  void setColorMode(ColorMode mode) { m_colorMode = mode; }

  bool isColorModeDetected() const { return m_colorModeDetected; }
  void setColorModeDetected(bool detected) { m_colorModeDetected = detected; }
  bool isAutomaticDetection() const { return m_automaticDetection; }
  void setAutomaticDetection(bool automatic) { m_automaticDetection = automatic; }
  int detectorSchemaVersion() const { return m_detectorSchemaVersion; }
  void setDetectorSchemaVersion(int version) { m_detectorSchemaVersion = version; }
  int detectionSensitivity() const { return m_detectionSensitivity; }
  void setDetectionSensitivity(int sensitivity) { m_detectionSensitivity = sensitivity; }

  bool isProcessed() const { return m_processed; }
  void setProcessed(bool processed) { m_processed = processed; }

  // Per-page force white balance (finds brightest pixels, assumes white)
  bool forceWhiteBalance() const { return m_forceWhiteBalance; }
  void setForceWhiteBalance(bool force) { m_forceWhiteBalance = force; }

 private:
  ColorMode m_colorMode;
  bool m_colorModeDetected;
  bool m_processed;
  bool m_forceWhiteBalance = false;
  bool m_automaticDetection = false;
  int m_detectorSchemaVersion = 0;
  int m_detectionSensitivity = -1;
};

class Settings {
  DECLARE_NON_COPYABLE(Settings)

 public:
  Settings();
  ~Settings();

  void clear();

  void performRelinking(const AbstractRelinker& relinker);

  void setParams(const PageId& pageId, const Params& params);
  std::unique_ptr<Params> getParams(const PageId& pageId) const;

  void setColorMode(const PageId& pageId, ColorMode mode);
  void setDetectedColorMode(const PageId& pageId, ColorMode mode);
  ColorMode getColorMode(const PageId& pageId) const;

  void setProcessed(const PageId& pageId, bool processed);
  bool isProcessed(const PageId& pageId) const;
  bool isColorModeDetectionNeeded(const PageId& pageId) const;

  void clearDetectionCache();
  void clearDetectionCacheForPage(const PageId& pageId);

  void setAutoColorModePolicy(AutoColorModePolicy policy);
  AutoColorModePolicy autoColorModePolicy() const;

  // Per-page force white balance
  void setForceWhiteBalance(const PageId& pageId, bool force);
  bool getForceWhiteBalance(const PageId& pageId) const;

  bool checkEverythingDefined(const PageSequence& pages, const PageId* ignore = nullptr) const;

  // Sensitivity adjustment for localized continuous-tone picture evidence.
  int midtoneThreshold() const;
  void setMidtoneThreshold(int threshold);

  // Output settings
  bool preserveOutput() const { return m_preserveOutput; }
  void setPreserveOutput(bool preserve) { m_preserveOutput = preserve; }

  QString outputPath() const { return m_outputPath; }
  void setOutputPath(const QString& path) { m_outputPath = path; }

  OutputFormat outputFormat() const { return m_outputFormat; }
  void setOutputFormat(OutputFormat format) { m_outputFormat = format; }

  TiffCompression tiffCompression() const { return m_tiffCompression; }
  // Also syncs the choice to the ApplicationSettings color-compression key
  // that TiffWriter actually reads (QW4; audit F2).
  void setTiffCompression(TiffCompression compression);

  int jpegQuality() const { return m_jpegQuality; }
  void setJpegQuality(int quality) { m_jpegQuality = quality; }

  // Auto white balance - corrects color cast from yellowed paper
  bool autoWhiteBalance() const { return m_autoWhiteBalance; }
  void setAutoWhiteBalance(bool enabled) { m_autoWhiteBalance = enabled; }

  // Get the effective output directory (user-chosen or temp)
  QString getEffectiveOutputDir(const QString& projectPath) const;

  // Get the temp directory path for a project
  static QString getTempOutputDir(const QString& projectPath);

 private:
  using PerPageParams = std::unordered_map<PageId, Params>;

  mutable QMutex m_mutex;
  PerPageParams m_perPageParams;
  int m_midtoneThreshold = 8;  // Default 8%

  // Output settings
  bool m_preserveOutput = false;
  QString m_outputPath;
  OutputFormat m_outputFormat = OutputFormat::TIFF;
  TiffCompression m_tiffCompression = TiffCompression::LZW;
  int m_jpegQuality = 90;

  // Auto white balance - enabled by default
  bool m_autoWhiteBalance = true;
  AutoColorModePolicy m_autoColorModePolicy = AutoColorModePolicy::BestGuess;
};

}  // namespace finalize
#endif  // SCANTAILOR_FINALIZE_SETTINGS_H_
