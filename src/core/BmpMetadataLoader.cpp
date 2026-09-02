// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BmpMetadataLoader.h"

#include <QIODevice>
#include <cstdlib>

#include "Dpm.h"
#include "ImageMetadata.h"

namespace {
// Reject obviously absurd dimensions.
constexpr qint64 kMaxDimension = 1000000;

constexpr int kFileHeaderLength = 14;

quint32 readLE32(const unsigned char* p) {
  return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint16 readLE16(const unsigned char* p) {
  return quint16(quint16(p[0]) | (quint16(p[1]) << 8));
}
}  // namespace

ImageMetadataLoader::Status BmpMetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                            const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  // Everything is peeked, so the device is left untouched throughout.
  unsigned char magic[2];
  if (ioDevice.peek((char*) magic, 2) != 2) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if ((magic[0] != 'B') || (magic[1] != 'M')) {
    return FORMAT_NOT_RECOGNIZED;
  }

  // The 14-byte file header is followed by the DIB header, whose first
  // 4 bytes give its own size and select its layout.
  unsigned char header[kFileHeaderLength + 40];
  if (ioDevice.peek((char*) header, kFileHeaderLength + 4) != kFileHeaderLength + 4) {
    return GENERIC_ERROR;
  }
  const quint32 dibHeaderLength = readLE32(header + kFileHeaderLength);
  const unsigned char* const dib = header + kFileHeaderLength;

  qint64 width = 0;
  qint64 height = 0;
  Dpi dpi;

  if (dibHeaderLength == 12) {
    // BITMAPCOREHEADER: 16-bit dimensions, no resolution fields.
    if (ioDevice.peek((char*) header, kFileHeaderLength + 12) != kFileHeaderLength + 12) {
      return GENERIC_ERROR;
    }
    width = readLE16(dib + 4);
    height = readLE16(dib + 6);
  } else if (dibHeaderLength >= 16) {
    // BITMAPINFOHEADER (40) and its extensions, plus OS/2 v2 headers
    // (16-64): 32-bit signed dimensions at the same offsets.  A negative
    // height marks a top-down bitmap.
    if (ioDevice.peek((char*) header, kFileHeaderLength + 12) != kFileHeaderLength + 12) {
      return GENERIC_ERROR;
    }
    width = std::abs(qint64(qint32(readLE32(dib + 4))));
    height = std::abs(qint64(qint32(readLE32(dib + 8))));

    if (dibHeaderLength >= 40) {
      // biXPelsPerMeter/biYPelsPerMeter live at DIB offsets 24 and 28.
      if (ioDevice.peek((char*) header, kFileHeaderLength + 32) != kFileHeaderLength + 32) {
        return GENERIC_ERROR;
      }
      const auto xPelsPerMeter = qint32(readLE32(dib + 24));
      const auto yPelsPerMeter = qint32(readLE32(dib + 28));
      if ((xPelsPerMeter > 0) && (yPelsPerMeter > 0)) {
        dpi = Dpm(xPelsPerMeter, yPelsPerMeter);
      }
    }
  } else {
    return GENERIC_ERROR;
  }

  if ((width <= 0) || (height <= 0) || (width > kMaxDimension) || (height > kMaxDimension)) {
    return GENERIC_ERROR;
  }

  out(ImageMetadata(QSize(int(width), int(height)), dpi));
  return LOADED;
}  // BmpMetadataLoader::loadMetadata
