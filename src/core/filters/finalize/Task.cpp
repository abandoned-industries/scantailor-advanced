// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"

#include <QDebug>
#include <QImage>
#include <cstdio>
#include <QPolygonF>
#include <utility>

#include "BinaryImage.h"
#include "Binarize.h"
#include "Filter.h"
#include "FilterData.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "Settings.h"
#include "TaskStatus.h"
#include "Thumbnail.h"
#include "WhiteBalance.h"
#include "AbstractOutputTask.h"
#include "filters/output/ColorParams.h"
#include "filters/output/Settings.h"
#include "LeptonicaDetector.h"
#include "PhotoFrameDetector.h"

namespace finalize {

class Task::UiUpdater : public FilterResult {
 public:
  UiUpdater(std::shared_ptr<Filter> filter,
            const PageId& pageId,
            const QImage& image,
            const ImageTransformation& xform,
            ColorMode colorMode,
            bool batchProcessing);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

 private:
  static QImage applyColorMode(const QImage& image, ColorMode mode);

  std::shared_ptr<Filter> m_filter;
  PageId m_pageId;
  QImage m_image;
  ImageTransformation m_xform;
  ColorMode m_colorMode;
  bool m_batchProcessing;
};

Task::Task(std::shared_ptr<Filter> filter,
           std::shared_ptr<AbstractOutputTask> nextTask,
           std::shared_ptr<Settings> settings,
           std::shared_ptr<output::Settings> outputSettings,
           AutoColorModePolicy autoColorModePolicy,
           const PageId& pageId,
           const bool batch)
    : m_filter(std::move(filter)),
      m_nextTask(std::move(nextTask)),
      m_settings(std::move(settings)),
      m_outputSettings(std::move(outputSettings)),
      m_autoColorModePolicy(autoColorModePolicy),
      m_pageId(pageId),
      m_batchProcessing(batch) {}

Task::~Task() = default;

FilterResultPtr Task::process(const TaskStatus& status, const FilterData& data, const QPolygonF& contentRectPhys) {
  status.throwIfCancelled();

  const QImage& image = data.origImage();
  const QRect contentBox = contentRectPhys.boundingRect().toRect().intersected(image.rect());

  // Consult the cached detection before doing any full-resolution preparation.
  std::unique_ptr<Params> existingParams = m_settings->getParams(m_pageId);

  // An explicit Output mode is authoritative, whether selected directly by
  // the user or applied by the Force B&W auto-process preset.
  if (m_outputSettings && !m_outputSettings->isParamsNull(m_pageId)) {
    output::Params explicitOutputParams = m_outputSettings->getParams(m_pageId);
    const output::ColorParams colorParams = explicitOutputParams.colorParams();
    if (colorParams.isColorModeUserSet()) {
      if (!explicitOutputParams.continuousToneRegions().isEmpty()
          || !explicitOutputParams.pictureFrames().isEmpty()) {
        explicitOutputParams.setContinuousToneRegions(output::ContinuousToneRegions());
        explicitOutputParams.setPictureFrames(output::PictureFrames());
        m_outputSettings->setParams(m_pageId, explicitOutputParams);
        m_outputSettings->removeOutputParams(m_pageId);
      }
      ColorMode mode = ColorMode::Grayscale;
      switch (colorParams.colorMode()) {
        case output::BLACK_AND_WHITE:
          mode = ColorMode::BlackAndWhite;
          break;
        case output::COLOR:
        case output::COLOR_GRAYSCALE:
          mode = ColorMode::Color;
          break;
        case output::MIXED:
          mode = ColorMode::Mixed;
          break;
        case output::GRAYSCALE:
        case output::AUTO_DETECT:
          break;
      }
      m_settings->setColorMode(m_pageId, mode);
      existingParams = m_settings->getParams(m_pageId);
    }
  }

  if (m_settings->isColorModeDetectionNeeded(m_pageId)) {
    QImage imageForDetection = image;
    const bool forceWB = m_settings->getForceWhiteBalance(m_pageId);

    if (forceWB) {
      // Force white balance: find brightest pixels and assume they should be white
      qDebug() << "Finalize: Force white balance ENABLED for this page";
      const QColor brightestColor = WhiteBalance::findBrightestPixels(image);
      if (brightestColor.isValid() && WhiteBalance::hasSignificantCast(brightestColor)) {
        imageForDetection = WhiteBalance::apply(image, brightestColor);
        qDebug() << "Finalize: applied FORCED white balance, brightest pixels color was" << brightestColor;
      }
    } else if (m_settings->autoWhiteBalance()) {
      qDebug() << "Finalize: Auto white balance ENABLED, detecting paper color...";
      const QColor paperColor = WhiteBalance::detectPaperColor(image, contentBox);
      qDebug() << "Finalize: Detected paper color:" << paperColor
               << "valid:" << paperColor.isValid()
               << "hasSignificantCast:" << WhiteBalance::hasSignificantCast(paperColor);
      if (paperColor.isValid() && WhiteBalance::hasSignificantCast(paperColor)) {
        // Keep the original chroma for detectWithCastCompensation(). Applying
        // a global gain here can neutralize the brightest paper sample while
        // leaving darker gradients chromatic. The detector would then see a
        // neutral background, lose the original cast direction, and keep a
        // false COLOR verdict.
        qDebug() << "Finalize: deferring paper-cast correction to color detector; paper color was" << paperColor;
      } else {
        qDebug() << "Finalize: NOT applying white balance - color invalid or no significant cast";
      }
    } else {
      qDebug() << "Finalize: Auto white balance DISABLED";
    }

    qDebug() << "Finalize: Running color mode detection (not cached)";
    // Crop to content area for detection (avoids analyzing gutters/margins)
    fprintf(stderr, "Crop: img=%dx%d content=%d,%d %dx%d\n",
            image.width(), image.height(),
            contentBox.x(), contentBox.y(), contentBox.width(), contentBox.height());
    if (contentBox.isValid() && contentBox.width() > 100 && contentBox.height() > 100) {
      detectColorMode(imageForDetection.copy(contentBox), contentBox, status);
    } else {
      // Fallback to full image if crop is too small
      detectColorMode(imageForDetection, imageForDetection.rect(), status);
    }
  } else {
    qDebug() << "Finalize: Skipping detection - already cached as"
             << static_cast<int>(existingParams->colorMode());

    const ColorMode cachedMode = existingParams->colorMode();
    const ColorMode policyMode = applyAutoColorModePolicy(cachedMode, m_autoColorModePolicy);

    // Keep the raw cached verdict in Finalize. Only the effective Output mode
    // is policy-constrained, and only auto-managed Output modes are updated.
    // This lets a later Best Guess run recover COLOR from a raw Color verdict.
    if (m_outputSettings) {
      output::ColorMode outputMode = output::GRAYSCALE;
      switch (policyMode) {
        case ColorMode::BlackAndWhite:
          outputMode = output::BLACK_AND_WHITE;
          break;
        case ColorMode::Color:
          outputMode = output::COLOR;
          break;
        case ColorMode::Mixed:
          outputMode = output::MIXED;
          break;
        case ColorMode::Grayscale:
          break;
      }

      output::Params outputParams = m_outputSettings->getParams(m_pageId);
      output::ColorParams colorParams = outputParams.colorParams();
      bool outputParamsChanged = false;
      const bool cachedAutomaticMixed =
          existingParams->isAutomaticDetection() && cachedMode == ColorMode::Mixed;
      if (!cachedAutomaticMixed && !outputParams.continuousToneRegions().isEmpty()) {
        outputParams.setContinuousToneRegions(output::ContinuousToneRegions());
        outputParamsChanged = true;
      }
      if (!cachedAutomaticMixed && !outputParams.pictureFrames().isEmpty()) {
        outputParams.setPictureFrames(output::PictureFrames());
        outputParamsChanged = true;
      }
      if (m_outputSettings->isParamsNull(m_pageId)
          || (!colorParams.isColorModeUserSet() && colorParams.colorMode() != outputMode)) {
        colorParams.setColorMode(outputMode);
        colorParams.setColorModeUserSet(false);
        colorParams.setColorModePresetSet(false);
        outputParams.setColorParams(colorParams);
        outputParamsChanged = true;
      }
      if (outputParamsChanged) {
        m_outputSettings->setParams(m_pageId, outputParams);
        m_outputSettings->removeOutputParams(m_pageId);
      }
    }
  }

  // Mark as processed
  m_settings->setProcessed(m_pageId, true);

  status.throwIfCancelled();

  // Pass to next task (output)
  if (m_nextTask) {
    return m_nextTask->process(status, data, contentRectPhys);
  }

  // Get the current color mode for display
  const ColorMode colorMode = m_settings->getColorMode(m_pageId);

  // If no next task, show UI
  return std::make_shared<UiUpdater>(m_filter, m_pageId, image, data.xform(), colorMode, m_batchProcessing);
}

void Task::detectColorMode(const QImage& image,
                           const QRect& sourceRect,
                           const TaskStatus& status) {
  ColorMode mode = ColorMode::Grayscale;
  output::ColorMode outputMode = output::GRAYSCALE;

  // Use Leptonica for document color type detection. Cast compensation
  // catches aged/toned paper whose tint would otherwise read as COLOR
  // even when margin-based white balance had nothing to sample.
  const int threshold = m_settings->midtoneThreshold();
  LeptonicaDetector::DetectionEvidence evidence;
  const LeptonicaDetector::ColorType colorType =
      LeptonicaDetector::detectWithCastCompensation(
          image, threshold, &evidence,
          LeptonicaDetector::AnalysisScalePolicy::productionDefault(), &status);

  switch (colorType) {
    case LeptonicaDetector::ColorType::BlackWhite:
      mode = ColorMode::BlackAndWhite;
      outputMode = output::BLACK_AND_WHITE;
      break;
    case LeptonicaDetector::ColorType::Color:
      mode = ColorMode::Color;
      outputMode = output::COLOR;
      break;
    case LeptonicaDetector::ColorType::Mixed:
      mode = ColorMode::Mixed;
      outputMode = output::MIXED;
      break;
    case LeptonicaDetector::ColorType::Grayscale:
    default:
      mode = ColorMode::Grayscale;
      outputMode = output::GRAYSCALE;
      break;
  }

  const ColorMode outputPolicyMode = applyAutoColorModePolicy(mode, m_autoColorModePolicy);
  switch (outputPolicyMode) {
    case ColorMode::BlackAndWhite:
      outputMode = output::BLACK_AND_WHITE;
      break;
    case ColorMode::Color:
      outputMode = output::COLOR;
      break;
    case ColorMode::Mixed:
      outputMode = output::MIXED;
      break;
    case ColorMode::Grayscale:
      outputMode = output::GRAYSCALE;
      break;
  }

  fprintf(stderr, "PAGE %d%s -> %s\n",
          m_pageId.imageId().page(),
          m_pageId.subPage() == PageId::LEFT_PAGE ? "L" : (m_pageId.subPage() == PageId::RIGHT_PAGE ? "R" : ""),
          LeptonicaDetector::colorTypeToString(colorType));
  fflush(stderr);

  // Store in finalize settings
  m_settings->setDetectedColorMode(m_pageId, mode);

  // Also set in output::Settings so output filter uses our detection
  if (m_outputSettings) {
    output::Params outputParams = m_outputSettings->getParams(m_pageId);
    output::ColorParams colorParams = outputParams.colorParams();
    colorParams.setColorMode(outputMode);
    colorParams.setColorModeUserSet(false);  // Mark as auto-detected, not user-set
    colorParams.setColorModePresetSet(false);
    outputParams.setColorParams(colorParams);
    output::ContinuousToneRegions continuousToneRegions;
    if (colorType == LeptonicaDetector::ColorType::Mixed
        && evidence.analysisSize.isValid()
        && !sourceRect.isEmpty()
        && !evidence.largestRegionBounds.isEmpty()) {
      continuousToneRegions.setAnalysisSize(evidence.analysisSize);
      continuousToneRegions.setSourceRect(sourceRect);
      continuousToneRegions.setBounds(
          evidence.regionBounds.isEmpty()
              ? QVector<QRect>{evidence.largestRegionBounds}
              : evidence.regionBounds);
    }
    outputParams.setContinuousToneRegions(continuousToneRegions);
    output::PictureFrames pictureFrames;
    if (colorType == LeptonicaDetector::ColorType::Mixed
        && evidence.analysisSize.isValid() && !sourceRect.isEmpty()) {
      const PhotoFrameDetector::Result frameResult =
          PhotoFrameDetector::detect(image, evidence);
      QVector<output::PictureFrame> acceptedFrames;
      acceptedFrames.reserve(frameResult.acceptedFrames.size());
      for (const PhotoFrameDetector::AcceptedFrame& frame :
           frameResult.acceptedFrames) {
        acceptedFrames.push_back(output::PictureFrame{frame.bounds, frame.reason});
      }
      if (!acceptedFrames.isEmpty()) {
        pictureFrames.setAnalysisSize(evidence.analysisSize);
        pictureFrames.setSourceRect(sourceRect);
        pictureFrames.setFrames(acceptedFrames);
      }
      qDebug() << "Finalize: frame proposals vision/cv/accepted"
               << frameResult.visionCandidates.size()
               << frameResult.cvCandidates.size()
               << frameResult.acceptedFrames.size();
    }
    outputParams.setPictureFrames(pictureFrames);
    m_outputSettings->setParams(m_pageId, outputParams);
    // A new evidence rectangle can change pixels even when the effective color
    // mode is still MIXED, so invalidate the prior rendered-output cache.
    m_outputSettings->removeOutputParams(m_pageId);
    qDebug() << "Finalize: set output color params for page" << m_pageId.imageId().filePath()
             << "outputMode:" << outputMode;
  }
}

Task::UiUpdater::UiUpdater(std::shared_ptr<Filter> filter,
                           const PageId& pageId,
                           const QImage& image,
                           const ImageTransformation& xform,
                           const ColorMode colorMode,
                           const bool batchProcessing)
    : m_filter(std::move(filter)),
      m_pageId(pageId),
      m_image(image),
      m_xform(xform),
      m_colorMode(colorMode),
      m_batchProcessing(batchProcessing) {}

QImage Task::UiUpdater::applyColorMode(const QImage& image, const ColorMode mode) {
  switch (mode) {
    case ColorMode::BlackAndWhite: {
      // Apply binarization using Otsu's method
      const imageproc::BinaryImage binaryImage = imageproc::binarizeOtsu(image);
      return binaryImage.toQImage();
    }
    case ColorMode::Grayscale: {
      // Convert to grayscale
      return image.convertToFormat(QImage::Format_Grayscale8);
    }
    case ColorMode::Color:
    case ColorMode::Mixed:
    default:
      // Keep original color
      return image;
  }
}

void Task::UiUpdater::updateUI(FilterUiInterface* ui) {
  // Notify the options widget about the current page
  OptionsWidget* optWidget = m_filter->optionsWidget();
  optWidget->postUpdateUI(m_pageId);
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  ui->invalidateThumbnail(m_pageId);

  if (m_batchProcessing) {
    return;
  }

  // Apply color mode transformation for preview
  const QImage displayImage = applyColorMode(m_image, m_colorMode);

  // Pass empty QImage for downscaled - ImageViewBase will create one automatically
  auto* view = new ImageView(displayImage, QImage(), m_xform);
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, nullptr);
}

}  // namespace finalize
