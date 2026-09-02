// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>

#include "AbstractRelinker.h"
#include "ApplicationSettings.h"
#include "PageSequence.h"
#include "RelinkablePath.h"
#include "LeptonicaDetector.h"

namespace finalize {

Params::Params()
    : m_colorMode(ColorMode::Grayscale),
      m_colorModeDetected(false),
      m_processed(false) {}

Params::Params(const QDomElement& el) {
  const QString colorModeStr = el.attribute("colorMode", "grayscale");
  if (colorModeStr == "bw") {
    m_colorMode = ColorMode::BlackAndWhite;
  } else if (colorModeStr == "mixed") {
    m_colorMode = ColorMode::Mixed;
  } else if (colorModeStr == "color") {
    m_colorMode = ColorMode::Color;
  } else {
    m_colorMode = ColorMode::Grayscale;
  }
  m_colorModeDetected = (el.attribute("detected", "0") == "1");
  m_processed = (el.attribute("processed", "0") == "1");
  m_forceWhiteBalance = (el.attribute("forceWB", "0") == "1");
  m_automaticDetection =
      (el.attribute("automaticDetection", m_colorModeDetected ? "1" : "0") == "1");
  m_detectorSchemaVersion = el.attribute("detectorVersion", "0").toInt();
  m_detectionSensitivity = el.attribute("detectionSensitivity", "-1").toInt();
}

QDomElement Params::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el = doc.createElement(name);

  QString colorModeStr;
  switch (m_colorMode) {
    case ColorMode::BlackAndWhite:
      colorModeStr = "bw";
      break;
    case ColorMode::Color:
      colorModeStr = "color";
      break;
    case ColorMode::Mixed:
      colorModeStr = "mixed";
      break;
    default:
      colorModeStr = "grayscale";
      break;
  }
  el.setAttribute("colorMode", colorModeStr);
  el.setAttribute("detected", m_colorModeDetected ? "1" : "0");
  el.setAttribute("processed", m_processed ? "1" : "0");
  if (m_forceWhiteBalance) {
    el.setAttribute("forceWB", "1");
  }
  if (m_colorModeDetected) {
    el.setAttribute("automaticDetection", m_automaticDetection ? "1" : "0");
  }
  if (m_automaticDetection) {
    el.setAttribute("detectorVersion", m_detectorSchemaVersion);
    el.setAttribute("detectionSensitivity", m_detectionSensitivity);
  }

  return el;
}

namespace {
// Explicit mapping between the finalize UI enum and the raw libtiff codes
// that ApplicationSettings stores and TiffWriter consumes (QW4; audit F2).
// The two vocabularies share no values: finalize {LZW=0, Deflate=1} vs
// libtiff COMPRESSION_LZW=5, COMPRESSION_ADOBE_DEFLATE=8.
constexpr int kLibtiffLzw = 5;           // COMPRESSION_LZW
constexpr int kLibtiffAdobeDeflate = 8;  // COMPRESSION_ADOBE_DEFLATE
}  // namespace

Settings::Settings() {
  // Reflect the persisted color-compression choice back into the UI-facing
  // value so the finalize control shows what TiffWriter will actually do.
  // Unknown or hand-edited codes keep the default display and are NOT
  // written back (writing happens only in setTiffCompression, i.e. on a user
  // action), so configurations that never touch the control keep their
  // current behavior exactly.
  if (ApplicationSettings::getInstance().getTiffColorCompression() == kLibtiffAdobeDeflate) {
    m_tiffCompression = TiffCompression::Deflate;
  }
}

Settings::~Settings() = default;

void Settings::setTiffCompression(const TiffCompression compression) {
  m_tiffCompression = compression;
  // Sync to the ApplicationSettings key TiffWriter actually reads
  // (settings/color_compression). The bitonal key (settings/bw_compression,
  // CCITT G4 by default) is deliberately untouched: the control offers only
  // the continuous-tone codecs LZW/Deflate, and G4 is the right codec for
  // bitonal TIFFs.
  ApplicationSettings::getInstance().setTiffColorCompression(
      compression == TiffCompression::Deflate ? kLibtiffAdobeDeflate : kLibtiffLzw);
}

void Settings::clear() {
  const QMutexLocker locker(&m_mutex);
  m_perPageParams.clear();
}

void Settings::performRelinking(const AbstractRelinker& relinker) {
  const QMutexLocker locker(&m_mutex);

  PerPageParams newParams;
  for (const auto& [pageId, params] : m_perPageParams) {
    const RelinkablePath oldPath(pageId.imageId().filePath(), RelinkablePath::File);
    PageId newPageId(pageId);
    newPageId.imageId().setFilePath(relinker.substitutionPathFor(oldPath));
    newParams.insert({newPageId, params});
  }
  m_perPageParams = std::move(newParams);
}

void Settings::setParams(const PageId& pageId, const Params& params) {
  const QMutexLocker locker(&m_mutex);
  m_perPageParams[pageId] = params;
}

std::unique_ptr<Params> Settings::getParams(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);
  const auto it = m_perPageParams.find(pageId);
  if (it != m_perPageParams.end()) {
    return std::make_unique<Params>(it->second);
  }
  return nullptr;
}

void Settings::setColorMode(const PageId& pageId, ColorMode mode) {
  const QMutexLocker locker(&m_mutex);
  auto& params = m_perPageParams[pageId];
  params.setColorMode(mode);
  params.setColorModeDetected(true);
  params.setAutomaticDetection(false);
}

void Settings::setDetectedColorMode(const PageId& pageId, ColorMode mode) {
  const QMutexLocker locker(&m_mutex);
  auto& params = m_perPageParams[pageId];
  params.setColorMode(mode);
  params.setColorModeDetected(true);
  params.setAutomaticDetection(true);
  params.setDetectorSchemaVersion(LeptonicaDetector::DETECTOR_SCHEMA_VERSION);
  params.setDetectionSensitivity(m_midtoneThreshold);
}

ColorMode Settings::getColorMode(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);
  const auto it = m_perPageParams.find(pageId);
  if (it != m_perPageParams.end()) {
    return it->second.colorMode();
  }
  return ColorMode::Grayscale;  // Default
}

void Settings::setProcessed(const PageId& pageId, bool processed) {
  const QMutexLocker locker(&m_mutex);
  m_perPageParams[pageId].setProcessed(processed);
}

bool Settings::isProcessed(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);
  const auto it = m_perPageParams.find(pageId);
  if (it != m_perPageParams.end()) {
    return it->second.isProcessed();
  }
  return false;
}

bool Settings::isColorModeDetectionNeeded(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);
  const auto it = m_perPageParams.find(pageId);
  if (it == m_perPageParams.end() || !it->second.isColorModeDetected()) return true;
  const Params& params = it->second;
  return params.isAutomaticDetection()
         && (params.detectorSchemaVersion() != LeptonicaDetector::DETECTOR_SCHEMA_VERSION
             || params.detectionSensitivity() != m_midtoneThreshold);
}

void Settings::clearDetectionCache() {
  const QMutexLocker locker(&m_mutex);
  for (auto& [pageId, params] : m_perPageParams) {
    params.setColorModeDetected(false);
    params.setAutomaticDetection(false);
    params.setProcessed(false);
  }
}

void Settings::clearDetectionCacheForPage(const PageId& pageId) {
  const QMutexLocker locker(&m_mutex);
  const auto it = m_perPageParams.find(pageId);
  if (it != m_perPageParams.end()) {
    it->second.setColorModeDetected(false);
    it->second.setAutomaticDetection(false);
    it->second.setProcessed(false);
  }
}

void Settings::setAutoColorModePolicy(AutoColorModePolicy policy) {
  const QMutexLocker locker(&m_mutex);
  m_autoColorModePolicy = policy;
}

AutoColorModePolicy Settings::autoColorModePolicy() const {
  const QMutexLocker locker(&m_mutex);
  return m_autoColorModePolicy;
}

int Settings::midtoneThreshold() const {
  const QMutexLocker locker(&m_mutex);
  return m_midtoneThreshold;
}

void Settings::setMidtoneThreshold(const int threshold) {
  const QMutexLocker locker(&m_mutex);
  if (m_midtoneThreshold == threshold) return;
  m_midtoneThreshold = threshold;
  for (auto& [pageId, params] : m_perPageParams) {
    if (params.isAutomaticDetection()) {
      params.setColorModeDetected(false);
      params.setProcessed(false);
    }
  }
}

void Settings::setForceWhiteBalance(const PageId& pageId, bool force) {
  const QMutexLocker locker(&m_mutex);
  m_perPageParams[pageId].setForceWhiteBalance(force);
}

bool Settings::getForceWhiteBalance(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);
  const auto it = m_perPageParams.find(pageId);
  if (it != m_perPageParams.end()) {
    return it->second.forceWhiteBalance();
  }
  return false;
}

bool Settings::checkEverythingDefined(const PageSequence& pages, const PageId* ignore) const {
  const QMutexLocker locker(&m_mutex);
  for (const PageInfo& pageInfo : pages) {
    if (ignore && (*ignore == pageInfo.id())) {
      continue;
    }
    const auto it = m_perPageParams.find(pageInfo.id());
    if (it == m_perPageParams.end() || !it->second.isProcessed()) {
      return false;
    }
  }
  return true;
}

QString Settings::getEffectiveOutputDir(const QString& projectPath) const {
  if (m_preserveOutput && !m_outputPath.isEmpty()) {
    return m_outputPath;
  }
  return getTempOutputDir(projectPath);
}

QString Settings::getTempOutputDir(const QString& projectPath) {
  // Create a hash of the project path for a unique temp folder name
  const QByteArray hash = QCryptographicHash::hash(projectPath.toUtf8(), QCryptographicHash::Md5).toHex().left(8);
  const QString tempBase = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  return tempBase + "/scantailor-spectre-" + QString::fromLatin1(hash);
}

}  // namespace finalize
