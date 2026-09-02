// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"

#include <DewarpingPointMapper.h>
#include <PolygonUtils.h>
#include <UnitsProvider.h>
#include <core/ApplicationSettings.h>
#include <core/TiffWriter.h>

#include <QDir>
#include <QPainter>
#include <algorithm>
#include <boost/bind/bind.hpp>
#include <cmath>
#include <utility>

#include "DebugImagesImpl.h"
#include "DespeckleState.h"
#include "DespeckleView.h"
#include "DespeckleVisualization.h"
#include "DewarpingView.h"
#include "Dpm.h"
#include "ErrorWidget.h"
#include "FillZoneComparator.h"
#include "FillZoneEditor.h"
#include "Filter.h"
#include "FilterData.h"
#include "FilterUiInterface.h"
#include "ImageLoader.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "OutputGenerator.h"
#include "OutputImageBuilder.h"
#include "OutputImageWithForeground.h"
#include "OutputImageWithOriginalBackground.h"
#include "PictureZoneComparator.h"
#include "PictureZoneEditor.h"
#include "RenderParams.h"
#include "Settings.h"
#include "TabbedImageView.h"
#include "TaskStatus.h"
#include "ThumbnailPixmapCache.h"
#include "Utils.h"
#include "weasel/TonalCurve.h"
#include <Transform.h>

using namespace imageproc;
using namespace dewarping;

namespace output {
namespace {
// Write output image in the appropriate format based on OutputFileNameGenerator settings
bool writeOutputImage(const QString& filePath, const QImage& image, const OutputFileNameGenerator& outFileNameGen) {
  const OutputImageFormat format = outFileNameGen.outputFormat();
  switch (format) {
    case OutputImageFormat::JPEG:
      return image.save(filePath, "JPEG", outFileNameGen.jpegQuality());
    case OutputImageFormat::PNG:
      return image.save(filePath, "PNG");
    case OutputImageFormat::TIFF:
    default:
      // TiffWriter reads its compression from ApplicationSettings;
      // finalize::Settings::setTiffCompression syncs the color-compression
      // key there when the user changes the finalize control (QW4).
      return TiffWriter::writeImage(filePath, image);
  }
}

class BatchUiUpdater : public FilterResult {
 public:
  BatchUiUpdater(std::shared_ptr<Filter> filter, const PageId& pageId)
      : m_filter(std::move(filter)), m_pageId(pageId) {}

  void updateUI(FilterUiInterface* ui) override { ui->invalidateThumbnail(m_pageId); }

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

 private:
  std::shared_ptr<Filter> m_filter;
  PageId m_pageId;
};
}  // namespace

class Task::UiUpdater : public FilterResult {
  Q_DECLARE_TR_FUNCTIONS(output::Task::UiUpdater)
 public:
  UiUpdater(std::shared_ptr<Filter> filter,
            std::shared_ptr<Settings> settings,
            std::unique_ptr<DebugImages> dbgImg,
            const Params& params,
            const ImageTransformation& xform,
            const QRect& virtContentRect,
            const PageId& pageId,
            const QImage& origImage,
            const QImage& outputImage,
            const BinaryImage& pictureMask,
            const DespeckleState& despeckleState,
            const DespeckleVisualization& despeckleVisualization,
            bool batch,
            bool debug);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

 private:
  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<Settings> m_settings;
  std::unique_ptr<DebugImages> m_dbg;
  Params m_params;
  ImageTransformation m_xform;
  QRect m_virtContentRect;
  PageId m_pageId;
  QImage m_origImage;
  QImage m_downscaledOrigImage;
  QImage m_outputImage;
  QImage m_downscaledOutputImage;
  BinaryImage m_pictureMask;
  DespeckleState m_despeckleState;
  DespeckleVisualization m_despeckleVisualization;
  bool m_batchProcessing;
  bool m_debug;
};


Task::Task(std::shared_ptr<Filter> filter,
           std::shared_ptr<Settings> settings,
           std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
           const PageId& pageId,
           const OutputFileNameGenerator& outFileNameGen,
           const ImageViewTab lastTab,
           const bool batch,
           const bool debug)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_thumbnailCache(std::move(thumbnailCache)),
      m_pageId(pageId),
      m_outFileNameGen(outFileNameGen),
      m_lastTab(lastTab),
      m_batchProcessing(batch),
      m_debug(debug) {
  if (debug) {
    m_dbg = std::make_unique<DebugImagesImpl>();
  }
}

Task::~Task() = default;

FilterResultPtr Task::process(const TaskStatus& status, const FilterData& data, const QPolygonF& contentRectPhys) {
  return process(status, data, contentRectPhys, nullptr);
}

FilterResultPtr Task::process(const TaskStatus& status,
                              const FilterData& data,
                              const QPolygonF& contentRectPhys,
                              QImage* outputImage) {
  status.throwIfCancelled();

  const QFileInfo sourceFileInfo(m_pageId.imageId().filePath());

  // Finalize is the authoritative color detector and populates Output settings
  // before this task runs. Missing params use Settings' safe defaults.
  Params params = m_settings->getParams(m_pageId);

  RenderParams renderParams(params.colorParams(), params.splittingOptions());

  ImageTransformation newXform(data.xform());
  newXform.postScaleToDpi(params.outputDpi());
  const QString outFilePath(m_outFileNameGen.filePathFor(m_pageId));
  const QFileInfo outFileInfo(outFilePath);
  const QString foregroundDir(Utils::foregroundDir(m_outFileNameGen.outDir()));
  const QString backgroundDir(Utils::backgroundDir(m_outFileNameGen.outDir()));
  const QString originalBackgroundDir(Utils::originalBackgroundDir(m_outFileNameGen.outDir()));
  const QString foregroundFilePath(QDir(foregroundDir).absoluteFilePath(outFileInfo.fileName()));
  const QFileInfo foregroundFileInfo(foregroundFilePath);
  const QString backgroundFilePath(QDir(backgroundDir).absoluteFilePath(outFileInfo.fileName()));
  const QFileInfo backgroundFileInfo(backgroundFilePath);
  const QString originalBackgroundFilePath(QDir(originalBackgroundDir).absoluteFilePath(outFileInfo.fileName()));
  const QFileInfo originalBackgroundFileInfo(originalBackgroundFilePath);

  const QString automaskDir(Utils::automaskDir(m_outFileNameGen.outDir()));
  const QString automaskFilePath(QDir(automaskDir).absoluteFilePath(outFileInfo.fileName()));
  QFileInfo automaskFileInfo(automaskFilePath);

  const QString specklesDir(Utils::specklesDir(m_outFileNameGen.outDir()));
  const QString specklesFilePath(QDir(specklesDir).absoluteFilePath(outFileInfo.fileName()));
  QFileInfo specklesFileInfo(specklesFilePath);

  const bool needPictureEditor = renderParams.mixedOutput() && !m_batchProcessing;
  const bool needSpecklesImage
      = ((params.despeckleLevel() != .0) && renderParams.needBinarization() && !m_batchProcessing);

  {
    std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(m_pageId));
    if (storedOutputParams != nullptr) {
      if (storedOutputParams->outputImageParams().getPictureShapeOptions() != params.pictureShapeOptions()) {
        // if picture shape options changed, reset auto picture zones
        OutputProcessingParams outputProcessingParams = m_settings->getOutputProcessingParams(m_pageId);
        outputProcessingParams.setAutoZonesFound(false);
        m_settings->setOutputProcessingParams(m_pageId, outputProcessingParams);
      }
    }
  }

  const OutputGenerator generator(newXform, contentRectPhys);

  OutputImageParams newOutputImageParams(generator.outputImageSize(), generator.outputContentRect(), newXform,
                                         params.outputDpi(), params.colorParams(), params.splittingOptions(),
                                         params.dewarpingOptions(), params.distortionModel(), params.depthPerception(),
                                         params.despeckleLevel(), params.pictureShapeOptions(),
                                         m_settings->getOutputProcessingParams(m_pageId), params.isBlackOnWhite(),
                                         m_settings->getForceWhiteBalance(m_pageId),
                                         m_settings->getManualWhiteBalanceColor(m_pageId));

  ZoneSet newPictureZones(m_settings->pictureZonesForPage(m_pageId));
  const ZoneSet newFillZones(m_settings->fillZonesForPage(m_pageId));

  bool needReprocess = false;
  do {  // Just to be able to break from it.
    std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(m_pageId));

    if (!storedOutputParams) {
      needReprocess = true;
      break;
    }

    if (!storedOutputParams->outputImageParams().matches(newOutputImageParams)) {
      needReprocess = true;
      break;
    }

    if (!PictureZoneComparator::equal(storedOutputParams->pictureZones(), newPictureZones)) {
      needReprocess = true;
      break;
    }

    if (!FillZoneComparator::equal(storedOutputParams->fillZones(), newFillZones)) {
      needReprocess = true;
      break;
    }

    if (!storedOutputParams->sourceFileParams().matches(OutputFileParams(sourceFileInfo))) {
      needReprocess = true;
      break;
    }
    if (!renderParams.splitOutput()) {
      if (!outFileInfo.exists()) {
        needReprocess = true;
        break;
      }
      if (!storedOutputParams->outputFileParams().matches(OutputFileParams(outFileInfo))) {
        needReprocess = true;
        break;
      }
    } else {
      if (!foregroundFileInfo.exists() || !backgroundFileInfo.exists()) {
        needReprocess = true;
        break;
      }
      if (!(storedOutputParams->foregroundFileParams().matches(OutputFileParams(foregroundFileInfo)))
          || !(storedOutputParams->backgroundFileParams().matches(OutputFileParams(backgroundFileInfo)))) {
        needReprocess = true;
        break;
      }
      if (renderParams.originalBackground()) {
        if (!originalBackgroundFileInfo.exists()) {
          needReprocess = true;
          break;
        }
        if (!(storedOutputParams->originalBackgroundFileParams().matches(
                OutputFileParams(originalBackgroundFileInfo)))) {
          needReprocess = true;
          break;
        }
      }
    }

    if (needPictureEditor) {
      if (!automaskFileInfo.exists()) {
        needReprocess = true;
        break;
      }

      if (!storedOutputParams->automaskFileParams().matches(OutputFileParams(automaskFileInfo))) {
        needReprocess = true;
        break;
      }
    }

    if (needSpecklesImage) {
      if (!specklesFileInfo.exists()) {
        needReprocess = true;
        break;
      }
      if (!storedOutputParams->specklesFileParams().matches(OutputFileParams(specklesFileInfo))) {
        needReprocess = true;
        break;
      }
    }
  } while (false);

  QImage outImg;
  BinaryImage automaskImg;
  BinaryImage specklesImg;

  if (!needReprocess) {
    QFile outFile(outFilePath);
    if (outFile.open(QIODevice::ReadOnly)) {
      outImg = ImageLoader::load(outFile, 0);
    }
    if (outImg.isNull() && renderParams.splitOutput()) {
      OutputImageBuilder imageBuilder;
      QFile foregroundFile(foregroundFilePath);
      if (foregroundFile.open(QIODevice::ReadOnly)) {
        imageBuilder.setForegroundImage(ImageLoader::load(foregroundFile, 0));
      }
      QFile backgroundFile(backgroundFilePath);
      if (backgroundFile.open(QIODevice::ReadOnly)) {
        imageBuilder.setBackgroundImage(ImageLoader::load(backgroundFile, 0));
      }
      if (renderParams.originalBackground()) {
        QFile originalBackgroundFile(originalBackgroundFilePath);
        if (originalBackgroundFile.open(QIODevice::ReadOnly)) {
          imageBuilder.setOriginalBackgroundImage(ImageLoader::load(originalBackgroundFile, 0));
        }
      }
      outImg = *imageBuilder.build();
    }
    needReprocess = outImg.isNull();

    if (needPictureEditor && !needReprocess) {
      QFile automaskFile(automaskFilePath);
      if (automaskFile.open(QIODevice::ReadOnly)) {
        automaskImg = BinaryImage(ImageLoader::load(automaskFile, 0));
      }
      needReprocess = automaskImg.isNull() || automaskImg.size() != outImg.size();
    }

    if (needSpecklesImage && !needReprocess) {
      QFile specklesFile(specklesFilePath);
      if (specklesFile.open(QIODevice::ReadOnly)) {
        specklesImg = BinaryImage(ImageLoader::load(specklesFile, 0));
      }
      needReprocess = specklesImg.isNull();
    }
  }

  if (needReprocess) {
    // Check if pass-through mode is enabled - skip all processing
    const OutputProcessingParams outputProcessingParams = m_settings->getOutputProcessingParams(m_pageId);
    const bool isPassThrough = outputProcessingParams.passThrough();

    if (isPassThrough) {
      // Pass-through mode: apply geometric transforms (page split, deskew) but skip heavy processing
      QImage origImage = data.origImage();

      // Apply photo adjustments (temp/tint + tonal curve) before the geometric transform,
      // matching the order OutputGenerator uses for the normal path.
      const weasel::PhotoAdjustments& adj = params.colorParams().photoAdjustments();
      if (!adj.isDefault()) {
        origImage = weasel::TonalCurve::apply(origImage, adj.temp(), adj.tint(), adj.exposure(),
                                              adj.contrast(), adj.highlights(), adj.shadows(),
                                              adj.whites(), adj.blacks());
      }

      const QRect outputRect = newXform.resultingRect().toRect();

      // Clip the output rect to the pre-crop area (page split boundary) so pixels
      // from the other half of a two-page spread don't bleed into the margins.
      const QPolygonF preCropArea = newXform.resultingPreCropArea();
      const QRect croppedRect = preCropArea.boundingRect().toRect().intersected(outputRect);

      // Transform source image into the clipped rect
      QImage croppedImg = transform(origImage, newXform.transform(), croppedRect,
                                    OutsidePixels::assumeColor(Qt::white));

      // Place onto white canvas of the full output size
      outImg = QImage(outputRect.size(), croppedImg.format());
      outImg.fill(Qt::white);
      const int offsetX = croppedRect.left() - outputRect.left();
      const int offsetY = croppedRect.top() - outputRect.top();
      {
        QPainter painter(&outImg);
        painter.drawImage(offsetX, offsetY, croppedImg);
      }

      // Apply brightness/contrast adjustments even in pass-through mode
      const double brightness = outputProcessingParams.brightness();
      const double contrast = outputProcessingParams.contrast();
      const bool autoLevels = outputProcessingParams.autoLevels();

      if (autoLevels || brightness != 0.0 || contrast != 0.0) {
        QImage work = outImg.convertToFormat(QImage::Format_ARGB32);
        const int w = work.width();
        const int h = work.height();

        // Auto levels: stretch histogram to full 0-255 range
        if (autoLevels) {
          int minVal = 255, maxVal = 0;
          for (int y = 0; y < h; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(work.constScanLine(y));
            for (int x = 0; x < w; ++x) {
              const QRgb p = line[x];
              const int gray = (qRed(p) + qGreen(p) + qBlue(p)) / 3;
              minVal = std::min(minVal, gray);
              maxVal = std::max(maxVal, gray);
            }
          }
          if (maxVal > minVal) {
            const double scale = 255.0 / (maxVal - minVal);
            for (int y = 0; y < h; ++y) {
              QRgb* line = reinterpret_cast<QRgb*>(work.scanLine(y));
              for (int x = 0; x < w; ++x) {
                const QRgb p = line[x];
                int r = static_cast<int>((qRed(p) - minVal) * scale);
                int g = static_cast<int>((qGreen(p) - minVal) * scale);
                int b = static_cast<int>((qBlue(p) - minVal) * scale);
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                line[x] = qRgb(r, g, b);
              }
            }
          }
        }

        // Apply brightness/contrast
        if (brightness != 0.0 || contrast != 0.0) {
          const int bShift = static_cast<int>(brightness * 255.0);
          const double cMul = std::max(0.2, std::pow(2.0, contrast * 1.3));

          for (int y = 0; y < h; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(work.scanLine(y));
            for (int x = 0; x < w; ++x) {
              const QRgb p = line[x];
              int r = static_cast<int>((qRed(p) - 128) * cMul + 128 + bShift);
              int g = static_cast<int>((qGreen(p) - 128) * cMul + 128 + bShift);
              int b = static_cast<int>((qBlue(p) - 128) * cMul + 128 + bShift);
              r = std::clamp(r, 0, 255);
              g = std::clamp(g, 0, 255);
              b = std::clamp(b, 0, 255);
              line[x] = qRgb(r, g, b);
            }
          }
        }

        outImg = work.convertToFormat(outImg.format());
      }

      bool invalidateParams = false;
      if (!writeOutputImage(outFilePath, outImg, m_outFileNameGen)) {
        invalidateParams = true;
      } else {
        deleteMutuallyExclusiveOutputFiles();
      }

      // Clean up any split output files since pass-through doesn't generate them
      QFile::remove(foregroundFilePath);
      QFile::remove(backgroundFilePath);
      QFile::remove(originalBackgroundFilePath);

      if (invalidateParams) {
        m_settings->removeOutputParams(m_pageId);
      } else {
        const OutputParams outParams(
            newOutputImageParams, OutputFileParams(sourceFileInfo), OutputFileParams(QFileInfo(outFilePath)),
            OutputFileParams(), OutputFileParams(), OutputFileParams(), OutputFileParams(), OutputFileParams(),
            newPictureZones, newFillZones);
        m_settings->setOutputParams(m_pageId, outParams);
      }

      m_thumbnailCache->recreateThumbnail(ImageId(outFilePath), outImg);
    } else {
      // Normal processing path
      // Even in batch processing mode we should still write automask, because it
      // will be needed when we view the results back in interactive mode.
      // The same applies even more to speckles file, as we need it not only
      // for visualization purposes, but also for re-doing despeckling at
      // different levels without going through the whole output generation process.
      const bool writeAutomask = renderParams.mixedOutput();
      const bool writeSpecklesFile = ((params.despeckleLevel() != .0) && renderParams.needBinarization());

      automaskImg = BinaryImage();
      specklesImg = BinaryImage();

      // OutputGenerator will write a new distortion model
      // there, if dewarping mode is AUTO.
      DistortionModel distortionModel;
      if (params.dewarpingOptions().dewarpingMode() == MANUAL) {
        distortionModel = params.distortionModel();
      }

      bool invalidateParams = false;
      {
        std::unique_ptr<OutputImage> outputImage
            = generator.process(status, data, newPictureZones, newFillZones, distortionModel, params.depthPerception(),
                                writeAutomask ? &automaskImg : nullptr, writeSpecklesFile ? &specklesImg : nullptr,
                                m_dbg.get(), m_pageId, m_settings);

      params = m_settings->getParams(m_pageId);

      if (((params.dewarpingOptions().dewarpingMode() == AUTO)
           || (params.dewarpingOptions().dewarpingMode() == MARGINAL))
          && distortionModel.isValid()) {
        // A new distortion model was generated.
        // We need to save it to be able to modify it manually.
        params.setDistortionModel(distortionModel);
        m_settings->setParams(m_pageId, params);
        newOutputImageParams.setDistortionModel(distortionModel);
      }

      // Saving refreshed params and output processing params.
      newOutputImageParams.setBlackOnWhite(m_settings->getParams(m_pageId).isBlackOnWhite());
      newOutputImageParams.setOutputProcessingParams(m_settings->getOutputProcessingParams(m_pageId));

      if (renderParams.splitOutput()) {
        auto* outputImageWithForeground = dynamic_cast<OutputImageWithForeground*>(outputImage.get());
        if (!outputImageWithForeground) {
          qWarning() << "Output: Failed to cast to OutputImageWithForeground for split output";
          invalidateParams = true;
        } else {
          QDir().mkdir(foregroundDir);
          QDir().mkdir(backgroundDir);
          if (!TiffWriter::writeImage(foregroundFilePath, outputImageWithForeground->getForegroundImage())
              || !TiffWriter::writeImage(backgroundFilePath, outputImageWithForeground->getBackgroundImage())) {
            invalidateParams = true;
          }
        }

        if (renderParams.originalBackground()) {
          auto* outputImageWithOrigBg = dynamic_cast<OutputImageWithOriginalBackground*>(outputImage.get());
          if (!outputImageWithOrigBg) {
            qWarning() << "Output: Failed to cast to OutputImageWithOriginalBackground";
            invalidateParams = true;
          } else {
            QDir().mkdir(originalBackgroundDir);
            if (!TiffWriter::writeImage(originalBackgroundFilePath,
                                        outputImageWithOrigBg->getOriginalBackgroundImage())) {
              invalidateParams = true;
            }
          }
        }
      }

      outImg = *outputImage;
    }

    if (!renderParams.originalBackground()) {
      QFile::remove(originalBackgroundFilePath);
    }
    if (!renderParams.splitOutput()) {
      QFile::remove(foregroundFilePath);
      QFile::remove(backgroundFilePath);
    }

    if (!writeOutputImage(outFilePath, outImg, m_outFileNameGen)) {
      invalidateParams = true;
    } else {
      deleteMutuallyExclusiveOutputFiles();
    }

    if (writeSpecklesFile && specklesImg.isNull()) {
      // Even if despeckling didn't actually take place, we still need
      // to write an empty speckles file.  Making it a special case
      // is simply not worth it.
      BinaryImage(outImg.size(), WHITE).swap(specklesImg);
    }

    if (writeAutomask) {
      // Note that QDir::mkdir() will fail if the parent directory,
      // that is $OUT/cache doesn't exist. We want that behaviour,
      // as otherwise when loading a project from a different machine,
      // a whole bunch of bogus directories would be created.
      QDir().mkdir(automaskDir);
      // Also note that QDir::mkdir() will fail if the directory already exists,
      // so we ignore its return value here.
      if (!TiffWriter::writeImage(automaskFilePath, automaskImg.toQImage())) {
        invalidateParams = true;
      }
    }
    if (writeSpecklesFile) {
      if (!QDir().mkpath(specklesDir)) {
        invalidateParams = true;
      } else if (!TiffWriter::writeImage(specklesFilePath, specklesImg.toQImage())) {
        invalidateParams = true;
      }
    }

    if (invalidateParams) {
      m_settings->removeOutputParams(m_pageId);
    } else {
      // Note that we can't reuse *_file_info objects
      // as we've just overwritten those files.
      const OutputParams outParams(
          newOutputImageParams, OutputFileParams(sourceFileInfo), OutputFileParams(QFileInfo(outFilePath)),
          renderParams.splitOutput() ? OutputFileParams(QFileInfo(foregroundFilePath)) : OutputFileParams(),
          renderParams.splitOutput() ? OutputFileParams(QFileInfo(backgroundFilePath)) : OutputFileParams(),
          renderParams.originalBackground() ? OutputFileParams(QFileInfo(originalBackgroundFilePath))
                                            : OutputFileParams(),
          writeAutomask ? OutputFileParams(QFileInfo(automaskFilePath)) : OutputFileParams(),
          writeSpecklesFile ? OutputFileParams(QFileInfo(specklesFilePath)) : OutputFileParams(), newPictureZones,
          newFillZones);

      m_settings->setOutputParams(m_pageId, outParams);
    }

    m_thumbnailCache->recreateThumbnail(ImageId(outFilePath), outImg);
    }  // end of else (normal processing path)
  }  // end of if (needReprocess)

  if (outputImage) {
    *outputImage = outImg;
  }

  if (m_batchProcessing) {
    return std::make_shared<BatchUiUpdater>(m_filter, m_pageId);
  }

  const DespeckleState despeckleState(outImg, specklesImg, params.despeckleLevel(), params.outputDpi());

  DespeckleVisualization despeckleVisualization;
  if (m_lastTab == TAB_DESPECKLING) {
    // Because constructing DespeckleVisualization takes a noticeable
    // amount of time, we only do it if we are sure we'll need it.
    // Otherwise it will get constructed on demand.
    despeckleVisualization = despeckleState.visualize();
  }
  return std::make_shared<UiUpdater>(m_filter, m_settings, std::move(m_dbg), params, newXform,
                                     generator.outputContentRect(), m_pageId, data.origImage(), outImg, automaskImg,
                                     despeckleState, despeckleVisualization, m_batchProcessing, m_debug);
}  // Task::process

/**
 * Delete output files mutually exclusive to m_pageId.
 */
void Task::deleteMutuallyExclusiveOutputFiles() {
  switch (m_pageId.subPage()) {
    case PageId::SINGLE_PAGE:
      QFile::remove(m_outFileNameGen.filePathFor(PageId(m_pageId.imageId(), PageId::LEFT_PAGE)));
      QFile::remove(m_outFileNameGen.filePathFor(PageId(m_pageId.imageId(), PageId::RIGHT_PAGE)));
      break;
    case PageId::LEFT_PAGE:
    case PageId::RIGHT_PAGE:
      QFile::remove(m_outFileNameGen.filePathFor(PageId(m_pageId.imageId(), PageId::SINGLE_PAGE)));
      break;
  }
}

/*============================ Task::UiUpdater ==========================*/

Task::UiUpdater::UiUpdater(std::shared_ptr<Filter> filter,
                           std::shared_ptr<Settings> settings,
                           std::unique_ptr<DebugImages> dbgImg,
                           const Params& params,
                           const ImageTransformation& xform,
                           const QRect& virtContentRect,
                           const PageId& pageId,
                           const QImage& origImage,
                           const QImage& outputImage,
                           const BinaryImage& pictureMask,
                           const DespeckleState& despeckleState,
                           const DespeckleVisualization& despeckleVisualization,
                           const bool batch,
                           const bool debug)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_dbg(std::move(dbgImg)),
      m_params(params),
      m_xform(xform),
      m_virtContentRect(virtContentRect),
      m_pageId(pageId),
      m_origImage(origImage),
      m_downscaledOrigImage(ImageView::createDownscaledImage(origImage)),
      m_outputImage(outputImage),
      m_downscaledOutputImage(ImageView::createDownscaledImage(outputImage)),
      m_pictureMask(pictureMask),
      m_despeckleState(despeckleState),
      m_despeckleVisualization(despeckleVisualization),
      m_batchProcessing(batch),
      m_debug(debug) {}

void Task::UiUpdater::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  ui->invalidateThumbnail(m_pageId);

  if (m_batchProcessing) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
  optWidget->postUpdateUI();
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto tabImageRectMap = std::make_unique<std::unordered_map<ImageViewTab, QRectF, std::hash<int>>>();

  auto imageView = std::make_unique<ImageView>(m_outputImage, m_downscaledOutputImage);
  const QPixmap downscaledOutputPixmap(imageView->downscaledPixmap());
  tabImageRectMap->insert(std::pair<ImageViewTab, QRectF>(TAB_OUTPUT, m_xform.resultingRect()));
  auto dewarpingView = std::make_unique<DewarpingView>(
      m_origImage, m_downscaledOrigImage, m_xform.transform(),
      PolygonUtils::convexHull(
          std::vector<QPointF>(m_xform.resultingPreCropArea().begin(), m_xform.resultingPreCropArea().end())),
      m_virtContentRect, m_pageId, m_params.dewarpingOptions(), m_params.distortionModel(),
      optWidget->depthPerception());
  const QPixmap downscaledOrigPixmap(dewarpingView->downscaledPixmap());
  QObject::connect(optWidget, SIGNAL(depthPerceptionChanged(double)), dewarpingView.get(),
                   SLOT(depthPerceptionChanged(double)));
  QObject::connect(dewarpingView.get(), SIGNAL(distortionModelChanged(const dewarping::DistortionModel&)), optWidget,
                   SLOT(distortionModelChanged(const dewarping::DistortionModel&)));
  tabImageRectMap->insert(
      std::pair<ImageViewTab, QRectF>(TAB_DEWARPING, m_xform.resultingPreCropArea().boundingRect()));

  std::unique_ptr<QWidget> pictureZoneEditor;
  if (m_pictureMask.isNull()) {
    pictureZoneEditor = std::make_unique<ErrorWidget>(tr("Picture zones are only available in Mixed mode."));
  } else {
    pictureZoneEditor = std::make_unique<output::PictureZoneEditor>(m_origImage, downscaledOrigPixmap, m_pictureMask,
                                                                    m_xform.transform(), m_xform.resultingPreCropArea(),
                                                                    m_pageId, m_settings);
    QObject::connect(pictureZoneEditor.get(), SIGNAL(invalidateThumbnail(const PageId&)), optWidget,
                     SIGNAL(invalidateThumbnail(const PageId&)));
    tabImageRectMap->insert(
        std::pair<ImageViewTab, QRectF>(TAB_PICTURE_ZONES, m_xform.resultingPreCropArea().boundingRect()));
  }

  // We make sure we never need to update the original <-> output
  // mapping at run time, that is without reloading.
  // In OptionsWidget::dewarpingChanged() we make sure to reload
  // if we are on the "Fill Zones" tab, and if not, it will be reloaded
  // anyway when another tab is selected.
  std::function<QPointF(const QPointF&)> origToOutput;
  std::function<QPointF(const QPointF&)> outputToOrig;
  if ((m_params.dewarpingOptions().dewarpingMode() != OFF) && m_params.distortionModel().isValid()) {
    const QTransform rotateXform
        = Utils::rotate(m_params.dewarpingOptions().getPostDeskewAngle(), m_xform.resultingRect().toRect());
    auto mapper = std::make_shared<DewarpingPointMapper>(m_params.distortionModel(), m_params.depthPerception().value(),
                                                         m_xform.transform(), m_virtContentRect, rotateXform);
    origToOutput = boost::bind(&DewarpingPointMapper::mapToDewarpedSpace, mapper, boost::placeholders::_1);
    outputToOrig = boost::bind(&DewarpingPointMapper::mapToWarpedSpace, mapper, boost::placeholders::_1);
  } else {
    using MapPointFunc = QPointF (QTransform::*)(const QPointF&) const;
    origToOutput = boost::bind((MapPointFunc) &QTransform::map, m_xform.transform(), boost::placeholders::_1);
    outputToOrig = boost::bind((MapPointFunc) &QTransform::map, m_xform.transformBack(), boost::placeholders::_1);
  }

  auto fillZoneEditor = std::make_unique<FillZoneEditor>(m_outputImage, downscaledOutputPixmap, origToOutput,
                                                         outputToOrig, m_pageId, m_settings);
  QObject::connect(fillZoneEditor.get(), SIGNAL(invalidateThumbnail(const PageId&)), optWidget,
                   SIGNAL(invalidateThumbnail(const PageId&)));
  tabImageRectMap->insert(std::pair<ImageViewTab, QRectF>(TAB_FILL_ZONES, m_xform.resultingRect()));

  std::unique_ptr<QWidget> despeckleView;
  if (m_params.colorParams().colorMode() == COLOR_GRAYSCALE) {
    despeckleView = std::make_unique<ErrorWidget>(tr("Despeckling can't be done in Color / Grayscale mode."));
  } else {
    despeckleView = std::make_unique<output::DespeckleView>(m_despeckleState, m_despeckleVisualization, m_debug);
    QObject::connect(optWidget, SIGNAL(despeckleLevelChanged(double, bool*)), despeckleView.get(),
                     SLOT(despeckleLevelChanged(double, bool*)));
    tabImageRectMap->insert(std::pair<ImageViewTab, QRectF>(TAB_DESPECKLING, m_xform.resultingRect()));
  }

  auto tabWidget = std::make_unique<TabbedImageView>();
  tabWidget->setDocumentMode(true);
  tabWidget->setTabPosition(QTabWidget::East);
  tabWidget->addTab(imageView.release(), tr("Output"), TAB_OUTPUT);
  tabWidget->addTab(pictureZoneEditor.release(), tr("Picture Zones"), TAB_PICTURE_ZONES);
  tabWidget->addTab(fillZoneEditor.release(), tr("Fill Zones"), TAB_FILL_ZONES);
  tabWidget->addTab(dewarpingView.release(), tr("Dewarping"), TAB_DEWARPING);
  tabWidget->addTab(despeckleView.release(), tr("Despeckling"), TAB_DESPECKLING);
  tabWidget->setCurrentTab(optWidget->lastTab());
  tabWidget->setImageRectMap(std::move(tabImageRectMap));

  QObject::connect(tabWidget.get(), SIGNAL(tabChanged(ImageViewTab)), optWidget, SLOT(tabChanged(ImageViewTab)));

  ui->setImageWidget(tabWidget.release(), ui->TRANSFER_OWNERSHIP, m_dbg.get());
}  // Task::UiUpdater::updateUI
}  // namespace output
