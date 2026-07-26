#include <BinaryImage.h>
#include <LeptonicaDetector.h>
#include <NullTaskStatus.h>
#include <PdfReader.h>
#include <PhotoFrameDetector.h>
#include <WhiteBalance.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "FilterData.h"
#include "ImageId.h"
#include "PageId.h"
#include "filters/output/ColorParams.h"
#include "filters/output/DepthPerception.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"

namespace {

QRect deriveContentRect(const QImage& image) {
  const QColor background = WhiteBalance::estimateBackgroundColor(image);
  const int backgroundLuma = background.isValid()
                                 ? (background.red() + background.green() + background.blue()) / 3
                                 : 220;
  const int inkThreshold = std::clamp(backgroundLuma - 35, 70, 190);
  const int guardX = std::max(1, image.width() * 3 / 100);
  const int guardY = std::max(1, image.height() * 2 / 100);
  const int step = std::max(1, std::min(image.width(), image.height()) / 1200);
  const QImage rgb = image.convertToFormat(QImage::Format_RGB32);

  int left = image.width();
  int top = image.height();
  int right = -1;
  int bottom = -1;
  for (int y = guardY; y < image.height() - guardY; y += step) {
    const auto* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
    for (int x = guardX; x < image.width() - guardX; x += step) {
      const int luma = (qRed(line[x]) + qGreen(line[x]) + qBlue(line[x])) / 3;
      if (luma < inkThreshold) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return image.rect();
  }
  return QRect(QPoint(left - image.width() * 2 / 100, top - image.height() * 2 / 100),
               QPoint(right + image.width() * 2 / 100, bottom + image.height() * 2 / 100))
      .intersected(image.rect());
}

QVector<QRect> mapAnalysisBounds(const output::ContinuousToneRegions& regions) {
  QVector<QRect> mapped;
  const QRect sourceRect = regions.sourceRect();
  const QSize analysisSize = regions.analysisSize();
  for (const QRect& analysisBounds : regions.bounds()) {
    mapped.push_back(
        QRectF(
            sourceRect.x()
                + analysisBounds.x() * static_cast<double>(sourceRect.width()) / analysisSize.width(),
            sourceRect.y()
                + analysisBounds.y() * static_cast<double>(sourceRect.height()) / analysisSize.height(),
            analysisBounds.width() * static_cast<double>(sourceRect.width()) / analysisSize.width(),
            analysisBounds.height() * static_cast<double>(sourceRect.height()) / analysisSize.height())
            .toAlignedRect());
  }
  return mapped;
}

QImage render(const QImage& source,
              const QString& inputPath,
              const int pdfPage,
              const QRect& contentRect,
              const output::ContinuousToneRegions& regions,
              const output::PictureFrames& frames,
              imageproc::BinaryImage* automask) {
  FilterData data(source);
  const PageId pageId(ImageId(inputPath, pdfPage + 1));
  auto settings = std::make_shared<output::Settings>();
  output::Params params;
  params.setOutputDpi(Dpi(300, 300));
  params.setDespeckleLevel(0.0);
  output::ColorParams colorParams = params.colorParams();
  colorParams.setColorMode(output::MIXED);
  colorParams.setColorModeUserSet(false);
  params.setColorParams(colorParams);
  params.setContinuousToneRegions(regions);
  params.setPictureFrames(frames);
  settings->setParams(pageId, params);

  QPolygonF contentPolygon{QRectF(contentRect)};
  output::OutputGenerator generator(data.xform(), contentPolygon);
  ZoneSet pictureZones;
  const ZoneSet fillZones;
  dewarping::DistortionModel distortionModel;
  NullTaskStatus status;
  std::unique_ptr<output::OutputImage> image = generator.process(
      status, data, pictureZones, fillZones, distortionModel,
      output::DepthPerception(), automask, nullptr, nullptr, pageId, settings);
  return image->toImage();
}

bool saveSideBySide(const QImage& before,
                    const QImage& after,
                    const QRect& cropRect,
                    const QString& path) {
  const QImage beforeCrop = before.copy(cropRect);
  const QImage afterCrop = after.copy(cropRect);
  QImage sideBySide(beforeCrop.width() + afterCrop.width(), beforeCrop.height(),
                    QImage::Format_RGB32);
  sideBySide.fill(Qt::white);
  QPainter painter(&sideBySide);
  painter.drawImage(QPoint(0, 0), beforeCrop);
  painter.drawImage(QPoint(beforeCrop.width(), 0), afterCrop);
  painter.end();
  return sideBySide.save(path);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  if (args.size() != 4) {
    std::fprintf(stderr, "usage: %s PDF PDF_PAGE_1_BASED OUTPUT_DIR\n", argv[0]);
    return 2;
  }

  bool pageOk = false;
  const int pdfPageOneBased = args[2].toInt(&pageOk);
  if (!pageOk || pdfPageOneBased < 1) {
    return 2;
  }
  const int pdfPage = pdfPageOneBased - 1;
  const QImage source = PdfReader::readImage(args[1], pdfPage, 300);
  if (source.isNull()) {
    std::fprintf(stderr, "unable to render PDF page\n");
    return 1;
  }
  const QRect contentRect = deriveContentRect(source);
  const QImage detectionCrop = source.copy(contentRect);
  LeptonicaDetector::DetectionEvidence evidence;
  const auto verdict = LeptonicaDetector::detectWithCastCompensation(
      detectionCrop, 8, &evidence,
      LeptonicaDetector::AnalysisScalePolicy::productionDefault());
  if (verdict != LeptonicaDetector::ColorType::Mixed
      || evidence.largestRegionBounds.isEmpty()) {
    std::fprintf(stderr, "page verdict is %s or has no region bounds\n",
                 LeptonicaDetector::colorTypeToString(verdict));
    return 1;
  }

  output::ContinuousToneRegions regions;
  regions.setAnalysisSize(evidence.analysisSize);
  regions.setSourceRect(contentRect);
  regions.setBounds(evidence.regionBounds.isEmpty()
                        ? QVector<QRect>{evidence.largestRegionBounds}
                        : evidence.regionBounds);
  const PhotoFrameDetector::Result detectedFrames =
      PhotoFrameDetector::detect(detectionCrop, evidence);
  output::PictureFrames frames;
  QVector<output::PictureFrame> persistedFrames;
  for (const auto& frame : detectedFrames.acceptedFrames) {
    persistedFrames.push_back({frame.bounds, frame.reason});
  }
  if (!persistedFrames.isEmpty()) {
    frames.setAnalysisSize(evidence.analysisSize);
    frames.setSourceRect(contentRect);
    frames.setFrames(persistedFrames);
  }

  QDir outputDir(args[3]);
  if (!outputDir.exists() && !QDir().mkpath(outputDir.path())) {
    return 1;
  }
  imageproc::BinaryImage beforeMask;
  imageproc::BinaryImage afterMask;
  const QImage before = render(source, args[1], pdfPage, contentRect,
                               regions, output::PictureFrames(), &beforeMask);
  const QImage after = render(source, args[1], pdfPage, contentRect,
                              regions, frames, &afterMask);
  const QString prefix = QStringLiteral("page-%1").arg(pdfPageOneBased, 3, 10, QLatin1Char('0'));
  if (!before.save(outputDir.filePath(prefix + "-before.png"))
      || !after.save(outputDir.filePath(prefix + "-after.png"))
      || !beforeMask.toQImage().save(outputDir.filePath(prefix + "-before-mask.png"))
      || !afterMask.toQImage().save(outputDir.filePath(prefix + "-after-mask.png"))) {
    return 1;
  }

  output::ContinuousToneRegions acceptedFrameRegions;
  if (!frames.isEmpty()) {
    acceptedFrameRegions.setAnalysisSize(frames.analysisSize());
    acceptedFrameRegions.setSourceRect(frames.sourceRect());
    QVector<QRect> acceptedBounds;
    for (const output::PictureFrame& frame : frames.frames()) {
      acceptedBounds.push_back(frame.bounds);
    }
    acceptedFrameRegions.setBounds(acceptedBounds);
  }
  QVector<QRect> mappedRegions = mapAnalysisBounds(acceptedFrameRegions);
  QRect mappedRegionUnion;
  for (QRect& rect : mappedRegions) {
    rect = rect.intersected(before.rect());
    mappedRegionUnion = mappedRegionUnion.united(rect);
  }
  const QRect cropRect = mappedRegionUnion.adjusted(-80, -80, 80, 80).intersected(before.rect());
  if (!saveSideBySide(before, after, cropRect,
                      outputDir.filePath(prefix + "-before-after-crop.png"))) {
    return 1;
  }

  qint64 insideDiff = 0;
  qint64 outsideDiff = 0;
  const QImage beforeRgb = before.convertToFormat(QImage::Format_RGB32);
  const QImage afterRgb = after.convertToFormat(QImage::Format_RGB32);
  for (int y = 0; y < beforeRgb.height(); ++y) {
    const auto* beforeLine = reinterpret_cast<const QRgb*>(beforeRgb.constScanLine(y));
    const auto* afterLine = reinterpret_cast<const QRgb*>(afterRgb.constScanLine(y));
    for (int x = 0; x < beforeRgb.width(); ++x) {
      if (beforeLine[x] != afterLine[x]) {
        bool inside = false;
        for (const QRect& rect : mappedRegions) {
          if (rect.contains(x, y)) {
            inside = true;
            break;
          }
        }
        (inside ? insideDiff : outsideDiff)++;
      }
    }
  }

  QFile metadata(outputDir.filePath(prefix + "-proof.txt"));
  if (!metadata.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return 1;
  }
  QTextStream stream(&metadata);
  stream << "pdf_page=" << pdfPageOneBased << '\n'
         << "source_size=" << source.width() << 'x' << source.height() << '\n'
         << "content_rect=" << contentRect.x() << ',' << contentRect.y() << ','
         << contentRect.width() << ',' << contentRect.height() << '\n'
         << "analysis_size=" << evidence.analysisSize.width() << 'x'
         << evidence.analysisSize.height() << '\n'
         << "analysis_region=" << evidence.largestRegionBounds.x() << ','
         << evidence.largestRegionBounds.y() << ','
         << evidence.largestRegionBounds.width() << ','
         << evidence.largestRegionBounds.height() << '\n'
         << "mapped_region_count=" << mappedRegions.size() << '\n';
  stream << "vision_candidate_count=" << detectedFrames.visionCandidates.size() << '\n'
         << "cv_candidate_count=" << detectedFrames.cvCandidates.size() << '\n'
         << "accepted_frame_count=" << detectedFrames.acceptedFrames.size() << '\n';
  for (int i = 0; i < detectedFrames.visionCandidates.size(); ++i) {
    const auto& candidate = detectedFrames.visionCandidates[i];
    stream << "vision_candidate_" << i << '=' << candidate.bounds.x() << ','
           << candidate.bounds.y() << ',' << candidate.bounds.width() << ','
           << candidate.bounds.height() << ",edge=" << candidate.edgeScore << '\n';
  }
  for (int i = 0; i < detectedFrames.cvCandidates.size(); ++i) {
    const auto& candidate = detectedFrames.cvCandidates[i];
    stream << "cv_candidate_" << i << '=' << candidate.bounds.x() << ','
           << candidate.bounds.y() << ',' << candidate.bounds.width() << ','
           << candidate.bounds.height() << ",edge=" << candidate.edgeScore << '\n';
  }
  for (int i = 0; i < detectedFrames.acceptedFrames.size(); ++i) {
    const auto& frame = detectedFrames.acceptedFrames[i];
    stream << "accepted_frame_" << i << '=' << frame.bounds.x() << ','
           << frame.bounds.y() << ',' << frame.bounds.width() << ','
           << frame.bounds.height() << ",reason=" << frame.reason << '\n';
  }
  for (int i = 0; i < mappedRegions.size(); ++i) {
    const QRect rect = mappedRegions[i];
    stream << "mapped_region_" << i << '=' << rect.x() << ',' << rect.y() << ','
           << rect.width() << ',' << rect.height() << '\n'
           << "before_mask_white_" << i << '=' << beforeMask.countWhitePixels(rect) << '\n'
           << "mapped_region_area_" << i << '=' << rect.width() * rect.height() << '\n';
  }
  stream
         << "inside_diff_pixels=" << insideDiff << '\n'
         << "outside_diff_pixels=" << outsideDiff << '\n';
  return outsideDiff == 0 ? 0 : 1;
}
