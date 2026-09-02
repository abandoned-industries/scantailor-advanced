// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputGeneratorUtils.h"

#include <BitOps.h>
#include <Constants.h>
#include <Grayscale.h>
#include <Morphology.h>
#include <PolygonRasterizer.h>
#include <SavGolFilter.h>
#include <Transform.h>
#include <XSpline.h>
#include <imageproc/ImageCombination.h>
#include <imageproc/PolygonUtils.h>

#include <QPainter>
#include <QPainterPath>
#include <boost/bind/bind.hpp>
#include <cmath>
#include <stdexcept>

#include "Dpi.h"
#include "FillColorProperty.h"
#include "PictureLayerProperty.h"
#include "ZoneCategoryProperty.h"

using namespace imageproc;

namespace output {
namespace detail {

template <typename PixelType>
PixelType reserveBlackAndWhite(PixelType color);

template <>
uint32_t reserveBlackAndWhite(uint32_t color) {
  // We handle both RGB32 and ARGB32 here.
  switch (color & 0x00FFFFFF) {
    case 0x00000000:
      return 0xFF010101;
    case 0x00FFFFFF:
      return 0xFFFEFEFE;
    default:
      return color;
  }
}

template <>
uint8_t reserveBlackAndWhite(uint8_t color) {
  switch (color) {
    case 0x00:
      return 0x01;
    case 0xFF:
      return 0xFE;
    default:
      return color;
  }
}

template <typename PixelType>
void reserveBlackAndWhite(QImage& img) {
  const int width = img.width();
  const int height = img.height();

  auto* imageLine = reinterpret_cast<PixelType*>(img.bits());
  const int imageStride = img.bytesPerLine() / sizeof(PixelType);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      imageLine[x] = reserveBlackAndWhite<PixelType>(imageLine[x]);
    }
    imageLine += imageStride;
  }
}

void reserveBlackAndWhite(QImage& img) {
  switch (img.format()) {
    case QImage::Format_Indexed8:
      reserveBlackAndWhite<uint8_t>(img);
      break;
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
      reserveBlackAndWhite<uint32_t>(img);
      break;
    default:
      throw std::invalid_argument("reserveBlackAndWhite: wrong image format.");
  }
}

template <typename PixelType>
void reserveBlackAndWhite(QImage& img, const BinaryImage& mask) {
  const int width = img.width();
  const int height = img.height();

  auto* imageLine = reinterpret_cast<PixelType*>(img.bits());
  const int imageStride = img.bytesPerLine() / sizeof(PixelType);
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        imageLine[x] = reserveBlackAndWhite<PixelType>(imageLine[x]);
      }
    }
    imageLine += imageStride;
    maskLine += maskStride;
  }
}

void reserveBlackAndWhite(QImage& img, const BinaryImage& mask) {
  switch (img.format()) {
    case QImage::Format_Indexed8:
      reserveBlackAndWhite<uint8_t>(img, mask);
      break;
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
      reserveBlackAndWhite<uint32_t>(img, mask);
      break;
    default:
      throw std::invalid_argument("reserveBlackAndWhite: wrong image format.");
  }
}

double paperMaskCoverage(const BinaryImage& mask) {
  if (mask.isNull() || mask.width() == 0 || mask.height() == 0) {
    return 0.0;
  }
  const uint32_t* data = mask.data();
  const int wordsPerLine = mask.wordsPerLine();
  size_t bitsSet = 0;
  for (int y = 0; y < mask.height(); ++y) {
    const uint32_t* line = data + y * wordsPerLine;
    for (int x = 0; x < wordsPerLine; ++x) {
      bitsSet += countNonZeroBits(line[x]);
    }
  }
  return bitsSet / static_cast<double>(mask.width() * mask.height());
}

void flattenBackgroundToPaper(QImage& img,
                              const BinaryImage& contentMask,
                              const QColor& paperColor,
                              int brightnessThreshold,
                              int saturationThreshold) {
  if (!paperColor.isValid()) {
    return;
  }

  // Use slightly stricter thresholds for replacement than for detection
  // (we want to be more conservative about what we replace)
  const int minBrightness = brightnessThreshold + 30;  // Higher brightness required
  const int maxSaturation = std::max(10, saturationThreshold - 15);  // Lower saturation required

  QImage work = img.convertToFormat(QImage::Format_ARGB32);
  const int w = work.width();
  const int h = work.height();
  const uint32_t* maskLine = contentMask.data();
  const int maskStride = contentMask.wordsPerLine();
  const uint32_t msb = uint32_t(1) << 31;
  QRgb* line = reinterpret_cast<QRgb*>(work.bits());
  const int stride = work.bytesPerLine() / 4;

  const int targetR = paperColor.red();
  const int targetG = paperColor.green();
  const int targetB = paperColor.blue();

  for (int y = 0; y < h; ++y, line += stride, maskLine += maskStride) {
    for (int x = 0; x < w; ++x) {
      if (!(maskLine[x >> 5] & (msb >> (x & 31)))) {
        continue;  // outside content area
      }
      const QRgb p = line[x];
      const int r = qRed(p);
      const int g = qGreen(p);
      const int b = qBlue(p);
      const int maxC = std::max({r, g, b});
      const int minC = std::min({r, g, b});
      const int saturation = maxC - minC;
      const int brightness = (r + g + b) / 3;

      // Only adjust bright, low-saturation pixels (background-like)
      if (brightness < minBrightness || saturation > maxSaturation) {
        continue;
      }

      line[x] = qRgb(targetR, targetG, targetB);
    }
  }

  img = work.convertToFormat(img.format());
}

template <typename MixedPixel>
void fillExcept(QImage& image, const BinaryImage& bwMask, const QColor& color) {
  auto* imageLine = reinterpret_cast<MixedPixel*>(image.bits());
  const int imageStride = image.bytesPerLine() / sizeof(MixedPixel);
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;
  const auto fillingPixel = static_cast<MixedPixel>(color.rgba());

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!(bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
        imageLine[x] = fillingPixel;
      }
    }
    imageLine += imageStride;
    bwMaskLine += bwMaskStride;
  }
}

void fillExcept(BinaryImage& image, const BinaryImage& bwMask, const BWColor color) {
  uint32_t* imageLine = image.data();
  const int imageStride = image.wordsPerLine();
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!(bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
        if (color == BLACK) {
          imageLine[x >> 5] |= (msb >> (x & 31));
        } else {
          imageLine[x >> 5] &= ~(msb >> (x & 31));
        }
      }
    }
    imageLine += imageStride;
    bwMaskLine += bwMaskStride;
  }
}

void BinaryImageXOR(BinaryImage& image, const BinaryImage& bwMask, const BWColor color) {
  uint32_t* imageLine = image.data();
  const int imageStride = image.wordsPerLine();
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;

  if (color == BLACK) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if ((imageLine[x >> 5] & (msb >> (x & 31))) != (bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
          imageLine[x >> 5] |= (msb >> (x & 31));
        } else {
          imageLine[x >> 5] &= ~(msb >> (x & 31));
        }
      }
      imageLine += imageStride;
      bwMaskLine += bwMaskStride;
    }
  } else {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if ((imageLine[x >> 5] & (msb >> (x & 31))) != (bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
          imageLine[x >> 5] &= ~(msb >> (x & 31));
        } else {
          imageLine[x >> 5] |= (msb >> (x & 31));
        }
      }
      imageLine += imageStride;
      bwMaskLine += bwMaskStride;
    }
  }
}

void fillMarginsInPlace(QImage& image,
                        const QPolygonF& contentPoly,
                        const QColor& color,
                        const bool antialiasing) {
  // Clip the content polygon to image bounds instead of throwing.
  // This handles edge cases from dewarping where the polygon may slightly exceed bounds.
  const QPolygonF clippedPoly = contentPoly.intersected(QRectF(image.rect()));
  if (clippedPoly.isEmpty()) {
    return;  // Nothing to preserve, entire image would be filled
  }

  if ((image.format() == QImage::Format_Mono) || (image.format() == QImage::Format_MonoLSB)) {
    BinaryImage binaryImage(image);
    PolygonRasterizer::fillExcept(binaryImage, (color == Qt::black) ? BLACK : WHITE, clippedPoly, Qt::WindingFill);
    image = binaryImage.toQImage();
    return;
  }
  if ((image.format() == QImage::Format_Indexed8) && image.isGrayscale()) {
    PolygonRasterizer::grayFillExcept(image, static_cast<unsigned char>(qGray(color.rgb())), clippedPoly,
                                      Qt::WindingFill);
    return;
  }

  assert(image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32);

  const QImage::Format imageFormat = image.format();
  image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);

    QPainterPath outerPath;
    outerPath.addRect(image.rect());
    QPainterPath innerPath;
    innerPath.addPolygon(PolygonUtils::round(clippedPoly));

    painter.drawPath(outerPath.subtracted(innerPath));
  }
  image = image.convertToFormat(imageFormat);
}

void fillMarginsInPlace(BinaryImage& image, const QPolygonF& contentPoly, const BWColor color) {
  // Clip the content polygon to image bounds instead of throwing.
  // This handles edge cases from dewarping where the polygon may slightly exceed bounds.
  const QPolygonF clippedPoly = contentPoly.intersected(QRectF(image.rect()));
  if (clippedPoly.isEmpty()) {
    return;  // Nothing to preserve, entire image would be filled
  }

  PolygonRasterizer::fillExcept(image, color, clippedPoly, Qt::WindingFill);
}

void fillMarginsInPlace(BinaryImage& image, const BinaryImage& contentMask, const BWColor color) {
  if (image.size() != contentMask.size()) {
    throw std::invalid_argument("fillMarginsInPlace: img and mask have different sizes");
  }

  fillExcept(image, contentMask, color);
}

void fillMarginsInPlace(QImage& image, const BinaryImage& contentMask, const QColor& color) {
  if (image.size() != contentMask.size()) {
    throw std::invalid_argument("fillMarginsInPlace: img and mask have different sizes");
  }

  if ((image.format() == QImage::Format_Mono) || (image.format() == QImage::Format_MonoLSB)) {
    BinaryImage binaryImage(image);
    fillExcept(binaryImage, contentMask, (color == Qt::black) ? BLACK : WHITE);
    image = binaryImage.toQImage();
    return;
  }

  if ((image.format() == QImage::Format_Indexed8) && image.isGrayscale()) {
    fillExcept<uint8_t>(image, contentMask, color);
  } else {
    assert(image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32);
    fillExcept<uint32_t>(image, contentMask, color);
  }
}

void removeAutoPictureZones(ZoneSet& pictureZones) {
  for (auto it = pictureZones.begin(); it != pictureZones.end();) {
    const Zone& zone = *it;
    if (zone.properties().locateOrDefault<ZoneCategoryProperty>()->zoneCategory() == ZoneCategoryProperty::AUTO) {
      it = pictureZones.erase(it);
    } else {
      ++it;
    }
  }
}

Zone createPictureZoneFromPoly(const QPolygonF& polygon) {
  PropertySet propertySet;
  propertySet.locateOrCreate<output::PictureLayerProperty>()->setLayer(output::PictureLayerProperty::ZONEPAINTER2);
  propertySet.locateOrCreate<output::ZoneCategoryProperty>()->setZoneCategory(ZoneCategoryProperty::AUTO);
  return Zone(SerializableSpline(polygon), propertySet);
}

void applyFillZonesInPlace(QImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput,
                           bool antialiasing) {
  if (zones.empty()) {
    return;
  }

  QImage canvas(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  {
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setPen(Qt::NoPen);

    for (const Zone& zone : zones) {
      const QColor color(zone.properties().locateOrDefault<FillColorProperty>()->color());
      const QPolygonF poly(zone.spline().transformed(origToOutput).toPolygon());
      painter.setBrush(color);
      painter.drawPolygon(poly, Qt::WindingFill);
    }
  }

  if ((img.format() == QImage::Format_Indexed8) && img.isGrayscale()) {
    img = toGrayscale(canvas);
  } else {
    img = canvas.convertToFormat(img.format());
  }
}

using MapPointFunc = QPointF (QTransform::*)(const QPointF&) const;

void applyFillZonesInPlace(QImage& img, const ZoneSet& zones, const QTransform& transform, bool antialiasing) {
  applyFillZonesInPlace(img, zones,
                        boost::bind(static_cast<MapPointFunc>(&QTransform::map), transform, boost::placeholders::_1),
                        antialiasing);
}

void applyFillZonesInPlace(BinaryImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput) {
  if (zones.empty()) {
    return;
  }

  for (const Zone& zone : zones) {
    const QColor color(zone.properties().locateOrDefault<FillColorProperty>()->color());
    const BWColor bwColor = qGray(color.rgb()) < 128 ? BLACK : WHITE;
    const QPolygonF poly(zone.spline().transformed(origToOutput).toPolygon());
    PolygonRasterizer::fill(img, bwColor, poly, Qt::WindingFill);
  }
}

void applyFillZonesInPlace(BinaryImage& img, const ZoneSet& zones, const QTransform& transform) {
  applyFillZonesInPlace(img, zones,
                        boost::bind(static_cast<MapPointFunc>(&QTransform::map), transform, boost::placeholders::_1));
}

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const std::function<QPointF(const QPointF&)>& origToOutput,
                                  const BinaryImage& pictureMask,
                                  bool binaryMode) {
  if (binaryMode) {
    BinaryImage bwContent(img, BinaryThreshold(1));
    applyFillZonesInPlace(bwContent, zones, origToOutput);
    applyFillZonesInPlace(img, zones, origToOutput);
    combineImages(img, bwContent, pictureMask);
  } else {
    QImage content(img);
    applyMask(content, pictureMask);
    applyFillZonesInPlace(content, zones, origToOutput, false);
    applyFillZonesInPlace(img, zones, origToOutput);
    combineImages(img, content, pictureMask);
  }
}

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const QTransform& transform,
                                  const BinaryImage& pictureMask,
                                  bool binaryMode) {
  applyFillZonesToMixedInPlace(
      img, zones, boost::bind(static_cast<MapPointFunc>(&QTransform::map), transform, boost::placeholders::_1),
      pictureMask, binaryMode);
}

void applyFillZonesToMask(BinaryImage& mask,
                          const ZoneSet& zones,
                          const std::function<QPointF(const QPointF&)>& origToOutput,
                          const BWColor fillColor) {
  if (zones.empty()) {
    return;
  }

  for (const Zone& zone : zones) {
    const QPolygonF poly(zone.spline().transformed(origToOutput).toPolygon());
    PolygonRasterizer::fill(mask, fillColor, poly, Qt::WindingFill);
  }
}

void applyFillZonesToMask(BinaryImage& mask,
                          const ZoneSet& zones,
                          const QTransform& transform,
                          const BWColor fillColor) {
  applyFillZonesToMask(mask, zones, boost::bind((MapPointFunc) &QTransform::map, transform, boost::placeholders::_1),
                       fillColor);
}

const int MultiplyDeBruijnBitPosition[32] = {0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
                                             31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};

const int MultiplyDeBruijnBitPosition2[32] = {0, 9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3, 30,
                                              8, 12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4, 31};

/**
 * aka "Count the consecutive zero bits (trailing) on the right with multiply and lookup"
 * from Bit Twiddling Hacks By Sean Eron Anderson
 *
 * https://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
 */
inline int countConsecutiveZeroBitsTrailing(uint32_t v) {
  return MultiplyDeBruijnBitPosition[((uint32_t) ((v & -signed(v)) * 0x077CB531U)) >> 27];
}

/**
 * aka "Find the log base 2 of an N-bit integer in O(lg(N)) operations with multiply and lookup"
 * from Bit Twiddling Hacks By Sean Eron Anderson
 *
 * https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
 */
inline int findPositionOfTheHighestBitSet(uint32_t v) {
  v |= v >> 1;  // first round down to one less than a power of 2
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return MultiplyDeBruijnBitPosition2[(uint32_t) (v * 0x07C4ACDDU) >> 27];
}

std::vector<QRect> findRectAreas(const BinaryImage& mask, BWColor contentColor, int sensitivity) {
  if (mask.isNull()) {
    return {};
  }

  std::vector<QRect> areas;

  const int w = mask.width();
  const int h = mask.height();
  const int wpl = mask.wordsPerLine();
  const int lastWordIdx = (w - 1) >> 5;
  const int lastWordBits = w - (lastWordIdx << 5);
  const int lastWordUnusedBits = 32 - lastWordBits;
  const uint32_t lastWordMask = ~uint32_t(0) << lastWordUnusedBits;
  const uint32_t modifier = (contentColor == WHITE) ? ~uint32_t(0) : 0;
  const uint32_t* const data = mask.data();

  const uint32_t* line = data;
  // create list of filled continuous blocks on each line
  for (int y = 0; y < h; ++y, line += wpl) {
    QRect area;
    area.setTop(y);
    area.setBottom(y);
    bool areaFound = false;
    for (int i = 0; i <= lastWordIdx; ++i) {
      uint32_t word = line[i] ^ modifier;
      if (i == lastWordIdx) {
        // The last (possibly incomplete) word.
        word &= lastWordMask;
      }
      if (word) {
        if (!areaFound) {
          area.setLeft((i << 5) + 31 - findPositionOfTheHighestBitSet(~line[i]));
          areaFound = true;
        }
        area.setRight(((i + 1) << 5) - 1);
      } else {
        if (areaFound) {
          uint32_t v = line[i - 1];
          if (v) {
            area.setRight(area.right() - countConsecutiveZeroBitsTrailing(~v));
          }
          areas.emplace_back(area);
          areaFound = false;
        }
      }
    }
    if (areaFound) {
      uint32_t v = line[lastWordIdx];
      if (v) {
        area.setRight(area.right() - countConsecutiveZeroBitsTrailing(~v));
      }
      areas.emplace_back(area);
    }
  }

  // join adjacent blocks of areas
  bool join = true;
  int overlap = 16;
  while (join) {
    join = false;
    std::vector<QRect> tmp;
    for (QRect area : areas) {
      // take an area and try to join with something in tmp
      QRect enlArea(area.adjusted(-overlap, -overlap, overlap, overlap));
      bool intersected = false;
      std::vector<QRect> tmp2;
      for (QRect ta : tmp) {
        QRect enlTA(ta.adjusted(-overlap, -overlap, overlap, overlap));
        if (enlArea.intersects(enlTA)) {
          intersected = true;
          join = true;
          tmp2.push_back(area.united(ta));
        } else {
          tmp2.push_back(ta);
        }
      }
      if (!intersected) {
        tmp2.push_back(area);
      }
      tmp = tmp2;
    }
    areas = tmp;
  }

  const auto percent = (float) (sensitivity / 100.);
  if (percent < 1.) {
    for (QRect& area : areas) {
      int wordWidth = area.width() >> 5;

      int left = area.left();
      int leftWord = left >> 5;
      int right = area.x() + area.width();
      int rightWord = right >> 5;
      int top = area.top();
      int bottom = area.bottom();

      const uint32_t* pdata = mask.data();

      const auto criterium = (int) (area.width() * percent);
      const auto criteriumWord = (int) (wordWidth * percent);

      // cut the dirty upper lines
      for (int y = top; y < bottom; y++) {
        line = pdata + wpl * y;

        int mword = 0;

        for (int k = leftWord; k < rightWord; k++) {
          if (!line[k]) {
            mword++;  // count the totally white words
          }
        }

        if (mword > criteriumWord) {
          area.setTop(y);
          break;
        }
      }

      // cut the dirty bottom lines
      for (int y = bottom; y > top; y--) {
        line = pdata + wpl * y;

        int mword = 0;

        for (int k = leftWord; k < rightWord; k++) {
          if (!line[k]) {
            mword++;
          }
        }

        if (mword > criteriumWord) {
          area.setBottom(y);
          break;
        }
      }

      for (int x = left; x < right; x++) {
        int mword = 0;

        for (int y = top; y < bottom; y++) {
          if (WHITE == mask.getPixel(x, y)) {
            mword++;
          }
        }

        if (mword > criterium) {
          area.setLeft(x);
          break;
        }
      }

      for (int x = right; x > left; x--) {
        int mword = 0;

        for (int y = top; y < bottom; y++) {
          if (WHITE == mask.getPixel(x, y)) {
            mword++;
          }
        }

        if (mword > criterium) {
          area.setRight(x);
          break;
        }
      }

      area = area.intersected(mask.rect());
    }
  }
  return areas;
}

void applyAffineTransform(QImage& image, const QTransform& xform, const QColor& outsideColor) {
  if (xform.isIdentity()) {
    return;
  }
  image = transform(image, xform, image.rect(), OutsidePixels::assumeWeakColor(outsideColor));
}

void applyAffineTransform(BinaryImage& image, const QTransform& xform, const BWColor outsideColor) {
  if (xform.isIdentity()) {
    return;
  }
  const QColor color = (outsideColor == BLACK) ? Qt::black : Qt::white;
  QImage converted = image.toQImage();
  applyAffineTransform(converted, xform, color);
  image = BinaryImage(converted);
}

void hitMissReplaceAllDirections(BinaryImage& img,
                                 const char* const pattern,
                                 const int patternWidth,
                                 const int patternHeight) {
  hitMissReplaceInPlace(img, WHITE, pattern, patternWidth, patternHeight);

  std::vector<char> patternData(static_cast<unsigned long long int>(patternWidth * patternHeight), ' ');
  char* const newPattern = &patternData[0];

  // Rotate 90 degrees clockwise.
  const char* p = pattern;
  int newWidth = patternHeight;
  int newHeight = patternWidth;
  for (int y = 0; y < patternHeight; ++y) {
    for (int x = 0; x < patternWidth; ++x, ++p) {
      const int newX = patternHeight - 1 - y;
      const int newY = x;
      newPattern[newY * newWidth + newX] = *p;
    }
  }
  hitMissReplaceInPlace(img, WHITE, newPattern, newWidth, newHeight);

  // Rotate upside down.
  p = pattern;
  newWidth = patternWidth;
  newHeight = patternHeight;
  for (int y = 0; y < patternHeight; ++y) {
    for (int x = 0; x < patternWidth; ++x, ++p) {
      const int newX = patternWidth - 1 - x;
      const int newY = patternHeight - 1 - y;
      newPattern[newY * newWidth + newX] = *p;
    }
  }
  hitMissReplaceInPlace(img, WHITE, newPattern, newWidth, newHeight);
  // Rotate 90 degrees counter-clockwise.
  p = pattern;
  newWidth = patternHeight;
  newHeight = patternWidth;
  for (int y = 0; y < patternHeight; ++y) {
    for (int x = 0; x < patternWidth; ++x, ++p) {
      const int newX = y;
      const int newY = patternWidth - 1 - x;
      newPattern[newY * newWidth + newX] = *p;
    }
  }
  hitMissReplaceInPlace(img, WHITE, newPattern, newWidth, newHeight);
}

QSize calcLocalWindowSize(const Dpi& dpi) {
  const QSizeF sizeMm(3, 30);
  const QSizeF sizeInch(sizeMm * constants::MM2INCH);
  const QSizeF sizePixelsF(dpi.horizontal() * sizeInch.width(), dpi.vertical() * sizeInch.height());
  QSize sizePixels(sizePixelsF.toSize());

  if (sizePixels.width() < 3) {
    sizePixels.setWidth(3);
  }
  if (sizePixels.height() < 3) {
    sizePixels.setHeight(3);
  }
  return sizePixels;
}

void movePointToTopMargin(BinaryImage& bwImage, XSpline& spline, int idx) {
  QPointF pos = spline.controlPointPosition(idx);

  for (int j = 0; j < pos.y(); j++) {
    if (bwImage.getPixel(static_cast<int>(pos.x()), j) == WHITE) {
      int count = 0;
      int checkNum = 16;

      for (int jj = j; jj < (j + checkNum); jj++) {
        if (bwImage.getPixel(static_cast<int>(pos.x()), jj) == WHITE) {
          count++;
        }
      }

      if (count == checkNum) {
        pos.setY(j);
        spline.moveControlPoint(idx, pos);
        break;
      }
    }
  }
}

void movePointToBottomMargin(BinaryImage& bwImage, XSpline& spline, int idx) {
  QPointF pos = spline.controlPointPosition(idx);

  for (int j = bwImage.height() - 1; j > pos.y(); j--) {
    if (bwImage.getPixel(static_cast<int>(pos.x()), j) == WHITE) {
      int count = 0;
      int checkNum = 16;

      for (int jj = j; jj > (j - checkNum); jj--) {
        if (bwImage.getPixel(static_cast<int>(pos.x()), jj) == WHITE) {
          count++;
        }
      }

      if (count == checkNum) {
        pos.setY(j);

        spline.moveControlPoint(idx, pos);

        break;
      }
    }
  }
}

void drawPoint(QImage& image, const QPointF& pt) {
  QPoint pts = pt.toPoint();

  for (int i = pts.x() - 10; i < pts.x() + 10; i++) {
    for (int j = pts.y() - 10; j < pts.y() + 10; j++) {
      QPoint p1(i, j);

      image.setPixel(p1, qRgb(255, 0, 0));
    }
  }
}

void movePointToTopMargin(BinaryImage& bwImage, std::vector<QPointF>& polyline, int idx) {
  QPointF& pos = polyline[idx];

  for (int j = 0; j < pos.y(); j++) {
    if (bwImage.getPixel(static_cast<int>(pos.x()), j) == WHITE) {
      int count = 0;
      int checkNum = 16;

      for (int jj = j; jj < (j + checkNum); jj++) {
        if (bwImage.getPixel(static_cast<int>(pos.x()), jj) == WHITE) {
          count++;
        }
      }

      if (count == checkNum) {
        pos.setY(j);

        break;
      }
    }
  }
}

void movePointToBottomMargin(BinaryImage& bwImage, std::vector<QPointF>& polyline, int idx) {
  QPointF& pos = polyline[idx];

  for (int j = bwImage.height() - 1; j > pos.y(); j--) {
    if (bwImage.getPixel(static_cast<int>(pos.x()), j) == WHITE) {
      int count = 0;
      int checkNum = 16;

      for (int jj = j; jj > (j - checkNum); jj--) {
        if (bwImage.getPixel(static_cast<int>(pos.x()), jj) == WHITE) {
          count++;
        }
      }

      if (count == checkNum) {
        pos.setY(j);

        break;
      }
    }
  }
}

float vertBorderSkewAngle(const QPointF& top, const QPointF& bottom) {
  return static_cast<float>(
      std::abs(std::atan((bottom.x() - top.x()) / (bottom.y() - top.y())) * 180.0 / constants::PI));
}

QImage smoothToGrayscale(const QImage& src, const Dpi& dpi) {
  const int minDpi = std::min(dpi.horizontal(), dpi.vertical());
  int window;
  int degree;
  if (minDpi <= 200) {
    window = 5;
    degree = 3;
  } else if (minDpi <= 400) {
    window = 7;
    degree = 4;
  } else if (minDpi <= 800) {
    window = 11;
    degree = 4;
  } else {
    window = 11;
    degree = 2;
  }
  return savGolFilter(src, QSize(window, window), degree, degree);
}

QSize from300dpi(const QSize& size, const Dpi& targetDpi) {
  const double hscale = targetDpi.horizontal() / 300.0;
  const double vscale = targetDpi.vertical() / 300.0;
  const int width = qRound(size.width() * hscale);
  const int height = qRound(size.height() * vscale);
  return QSize(std::max(1, width), std::max(1, height));
}

QSize to300dpi(const QSize& size, const Dpi& sourceDpi) {
  const double hscale = 300.0 / sourceDpi.horizontal();
  const double vscale = 300.0 / sourceDpi.vertical();
  const int width = qRound(size.width() * hscale);
  const int height = qRound(size.height() * vscale);
  return QSize(std::max(1, width), std::max(1, height));
}

}  // namespace detail
}  // namespace output
