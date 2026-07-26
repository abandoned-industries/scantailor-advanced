#ifndef SCANTAILOR_OUTPUT_PICTUREREGIONMASK_H_
#define SCANTAILOR_OUTPUT_PICTUREREGIONMASK_H_

#include <QPoint>
#include <QRect>
#include <QTransform>
#include <QVector>

#include "Params.h"

namespace imageproc {
class BinaryImage;
}

namespace output {

struct PictureRegionFillResult {
  QVector<QRect> mappedRegions;
  int filledRegionCount = 0;
};

/**
 * Maps Finalize's analysis-space continuous-tone bounds into mask coordinates
 * and fills holes only in regions whose existing white (picture) coverage is
 * at least minPictureCoverage.
 */
PictureRegionFillResult fillPictureRegionHoles(
    imageproc::BinaryImage& pictureMask,
    const ContinuousToneRegions& regions,
    const QTransform& originalToOutput,
    const QPoint& maskOriginInOutput,
    double minPictureCoverage = 0.40);

/**
 * Paint accepted printed-photo frames white exactly inside their mapped
 * bounds.  Unlike coarse Stage-4 regions this has no coverage gate.
 */
PictureRegionFillResult fillPictureFrames(
    imageproc::BinaryImage& pictureMask,
    const PictureFrames& frames,
    const QTransform& originalToOutput,
    const QPoint& maskOriginInOutput);

}  // namespace output

#endif  // SCANTAILOR_OUTPUT_PICTUREREGIONMASK_H_
