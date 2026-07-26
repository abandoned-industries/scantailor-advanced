#include "PictureRegionMask.h"

#include <BWColor.h>
#include <BinaryImage.h>

#include <QPolygonF>
#include <QtGlobal>

namespace output {

namespace {

QRect mapAnalysisRect(const QRect& analysisBounds,
                      const QSize& analysisSize,
                      const QRect& sourceRect,
                      const QTransform& originalToOutput,
                      const QPoint& maskOriginInOutput,
                      const QRect& maskRect) {
  const QRect clippedAnalysisBounds =
      analysisBounds.intersected(QRect(QPoint(0, 0), analysisSize));
  if (clippedAnalysisBounds.isEmpty()) {
    return QRect();
  }
  const double scaleX = static_cast<double>(sourceRect.width()) / analysisSize.width();
  const double scaleY = static_cast<double>(sourceRect.height()) / analysisSize.height();
  const QRectF originalBounds(
      sourceRect.x() + clippedAnalysisBounds.x() * scaleX,
      sourceRect.y() + clippedAnalysisBounds.y() * scaleY,
      clippedAnalysisBounds.width() * scaleX,
      clippedAnalysisBounds.height() * scaleY);
  return originalToOutput.mapRect(originalBounds)
      .translated(-maskOriginInOutput)
      .toAlignedRect()
      .intersected(maskRect);
}

}  // namespace

PictureRegionFillResult fillPictureRegionHoles(
    imageproc::BinaryImage& pictureMask,
    const ContinuousToneRegions& regions,
    const QTransform& originalToOutput,
    const QPoint& maskOriginInOutput,
    const double minPictureCoverage) {
  PictureRegionFillResult result;
  if (pictureMask.isNull() || regions.isEmpty()) {
    return result;
  }

  const QSize analysisSize = regions.analysisSize();
  const QRect sourceRect = regions.sourceRect();
  for (const QRect& analysisBounds : regions.bounds()) {
    const QRect maskBounds = mapAnalysisRect(
        analysisBounds, analysisSize, sourceRect, originalToOutput,
        maskOriginInOutput, pictureMask.rect());
    if (maskBounds.isEmpty()) {
      continue;
    }

    result.mappedRegions.push_back(maskBounds);
    const int area = maskBounds.width() * maskBounds.height();
    const double coverage = static_cast<double>(
                                pictureMask.countWhitePixels(maskBounds))
                            / area;
    if (coverage + 1e-12 >= qBound(0.0, minPictureCoverage, 1.0)) {
      // White is the picture polarity: these pixels preserve the continuous-
      // tone source during MIXED compositing.  The fill is clipped to the
      // detector's mapped bounds and therefore cannot grow into nearby text.
      pictureMask.fill(maskBounds, imageproc::WHITE);
      ++result.filledRegionCount;
    }
  }
  return result;
}

PictureRegionFillResult fillPictureFrames(
    imageproc::BinaryImage& pictureMask,
    const PictureFrames& frames,
    const QTransform& originalToOutput,
    const QPoint& maskOriginInOutput) {
  PictureRegionFillResult result;
  if (pictureMask.isNull() || frames.isEmpty()) {
    return result;
  }
  for (const PictureFrame& frame : frames.frames()) {
    const QRect maskBounds = mapAnalysisRect(
        frame.bounds, frames.analysisSize(), frames.sourceRect(),
        originalToOutput, maskOriginInOutput, pictureMask.rect());
    if (maskBounds.isEmpty()) {
      continue;
    }
    result.mappedRegions.push_back(maskBounds);
    pictureMask.fill(maskBounds, imageproc::WHITE);
    ++result.filledRegionCount;
  }
  return result;
}

}  // namespace output
