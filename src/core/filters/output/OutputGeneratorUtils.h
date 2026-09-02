// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTGENERATORUTILS_H_
#define SCANTAILOR_OUTPUT_OUTPUTGENERATORUTILS_H_

#include <BWColor.h>
#include <BinaryImage.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QSize>
#include <QTransform>
#include <cstdint>
#include <functional>
#include <vector>

#include "Zone.h"
#include "ZoneSet.h"

class Dpi;
class XSpline;

namespace output {
// Internal helper library for OutputGenerator, moved verbatim from the
// anonymous namespace at the top of OutputGenerator.cpp. Not exported
// outside the output filter.
namespace detail {

struct RaiseAboveBackground {
  static uint8_t transform(uint8_t src, uint8_t dst) {
    // src: orig pixel value
    // dst: background estimate
    // Guard against division by zero: if background is 0 or <= src,
    // return white (maximum illumination correction).
    // Note: the old check "dst - src < 1" used unsigned arithmetic which
    // would underflow when dst < src, failing to protect against dst == 0.
    if (dst == 0 || dst <= src) {
      return 0xff;
    }
    const unsigned orig = src;
    const unsigned background = dst;
    return static_cast<uint8_t>((orig * 255 + background / 2) / background);
  }
};

struct CombineInverted {
  static uint8_t transform(uint8_t src, uint8_t dst) {
    const unsigned dilated = dst;
    const unsigned eroded = src;
    const unsigned res = 255 - (255 - dilated) * eroded / 255;
    return static_cast<uint8_t>(res);
  }
};

void reserveBlackAndWhite(QImage& img);

void reserveBlackAndWhite(QImage& img, const imageproc::BinaryImage& mask);

double paperMaskCoverage(const imageproc::BinaryImage& mask);

void flattenBackgroundToPaper(QImage& img,
                              const imageproc::BinaryImage& contentMask,
                              const QColor& paperColor,
                              int brightnessThreshold,
                              int saturationThreshold);

void BinaryImageXOR(imageproc::BinaryImage& image, const imageproc::BinaryImage& bwMask, imageproc::BWColor color);

void fillMarginsInPlace(QImage& image, const QPolygonF& contentPoly, const QColor& color, bool antialiasing = true);

void fillMarginsInPlace(imageproc::BinaryImage& image, const QPolygonF& contentPoly, imageproc::BWColor color);

void fillMarginsInPlace(imageproc::BinaryImage& image,
                        const imageproc::BinaryImage& contentMask,
                        imageproc::BWColor color);

void fillMarginsInPlace(QImage& image, const imageproc::BinaryImage& contentMask, const QColor& color);

void removeAutoPictureZones(ZoneSet& pictureZones);

Zone createPictureZoneFromPoly(const QPolygonF& polygon);

void applyFillZonesInPlace(QImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput,
                           bool antialiasing = true);

void applyFillZonesInPlace(QImage& img, const ZoneSet& zones, const QTransform& transform, bool antialiasing = true);

void applyFillZonesInPlace(imageproc::BinaryImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput);

void applyFillZonesInPlace(imageproc::BinaryImage& img, const ZoneSet& zones, const QTransform& transform);

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const std::function<QPointF(const QPointF&)>& origToOutput,
                                  const imageproc::BinaryImage& pictureMask,
                                  bool binaryMode);

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const QTransform& transform,
                                  const imageproc::BinaryImage& pictureMask,
                                  bool binaryMode);

void applyFillZonesToMask(imageproc::BinaryImage& mask,
                          const ZoneSet& zones,
                          const std::function<QPointF(const QPointF&)>& origToOutput,
                          imageproc::BWColor fillColor = imageproc::BLACK);

void applyFillZonesToMask(imageproc::BinaryImage& mask,
                          const ZoneSet& zones,
                          const QTransform& transform,
                          imageproc::BWColor fillColor = imageproc::BLACK);

std::vector<QRect> findRectAreas(const imageproc::BinaryImage& mask, imageproc::BWColor contentColor, int sensitivity);

void applyAffineTransform(QImage& image, const QTransform& xform, const QColor& outsideColor);

void applyAffineTransform(imageproc::BinaryImage& image, const QTransform& xform, imageproc::BWColor outsideColor);

void hitMissReplaceAllDirections(imageproc::BinaryImage& img,
                                 const char* pattern,
                                 int patternWidth,
                                 int patternHeight);

QSize calcLocalWindowSize(const Dpi& dpi);

void movePointToTopMargin(imageproc::BinaryImage& bwImage, XSpline& spline, int idx);

void movePointToBottomMargin(imageproc::BinaryImage& bwImage, XSpline& spline, int idx);

void drawPoint(QImage& image, const QPointF& pt);

void movePointToTopMargin(imageproc::BinaryImage& bwImage, std::vector<QPointF>& polyline, int idx);

void movePointToBottomMargin(imageproc::BinaryImage& bwImage, std::vector<QPointF>& polyline, int idx);

float vertBorderSkewAngle(const QPointF& top, const QPointF& bottom);

QImage smoothToGrayscale(const QImage& src, const Dpi& dpi);

QSize from300dpi(const QSize& size, const Dpi& targetDpi);

QSize to300dpi(const QSize& size, const Dpi& sourceDpi);

}  // namespace detail
}  // namespace output

#endif  // SCANTAILOR_OUTPUT_OUTPUTGENERATORUTILS_H_
