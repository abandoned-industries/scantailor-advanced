// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "WebpMetadataLoader.h"

#include <QIODevice>
#include <algorithm>
#include <cstring>

#include "ImageMetadata.h"

namespace {
// Reject obviously absurd dimensions (WebP itself caps at 16383/chunk
// dimension anyway; VP8X canvases can be larger).
constexpr qint64 kMaxDimension = 1000000;

// Stop scanning for an image chunk after this many chunks.  The spec puts
// VP8X first when present, and VP8/VP8L directly after the header
// otherwise, so anything past a couple of chunks is malformed.
constexpr int kMaxChunksScanned = 8;

quint32 readLE32(const unsigned char* p) {
  return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint16 readLE16(const unsigned char* p) {
  return quint16(quint16(p[0]) | (quint16(p[1]) << 8));
}

quint32 readLE24(const unsigned char* p) {
  return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16);
}

bool readExact(QIODevice& ioDevice, unsigned char* buf, qint64 numBytes) {
  while (numBytes > 0) {
    const qint64 read = ioDevice.read((char*) buf, numBytes);
    if (read <= 0) {
      return false;
    }
    buf += read;
    numBytes -= read;
  }
  return true;
}

bool skipBytes(QIODevice& ioDevice, qint64 numBytes) {
  unsigned char buf[4096];
  while (numBytes > 0) {
    const qint64 chunk = std::min<qint64>(numBytes, sizeof(buf));
    const qint64 read = ioDevice.read((char*) buf, chunk);
    if (read <= 0) {
      return false;
    }
    numBytes -= read;
  }
  return true;
}
}  // namespace

ImageMetadataLoader::Status WebpMetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                             const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  // The 12-byte container header: 'RIFF' + file size + 'WEBP'.  Peeked, so
  // the device is left untouched on a mismatch.
  unsigned char header[12];
  if (ioDevice.peek((char*) header, sizeof(header)) != qint64(sizeof(header))) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if ((std::memcmp(header, "RIFF", 4) != 0) || (std::memcmp(header + 8, "WEBP", 4) != 0)) {
    return FORMAT_NOT_RECOGNIZED;
  }

  // The signature matched.  From here on the device may be consumed and any
  // malformed structure is a GENERIC_ERROR, like in the other loaders.
  if (!skipBytes(ioDevice, sizeof(header))) {
    return GENERIC_ERROR;
  }

  qint64 width = 0;
  qint64 height = 0;
  bool haveDimensions = false;

  for (int i = 0; !haveDimensions && (i < kMaxChunksScanned); ++i) {
    unsigned char chunkHeader[8];
    if (!readExact(ioDevice, chunkHeader, sizeof(chunkHeader))) {
      return GENERIC_ERROR;
    }
    const quint32 chunkSize = readLE32(chunkHeader + 4);
    // Chunks are padded to even sizes.
    const qint64 paddedSize = qint64(chunkSize) + (chunkSize & 1);

    if (std::memcmp(chunkHeader, "VP8 ", 4) == 0) {
      // Lossy: 3-byte frame tag, 3-byte sync code, then 14-bit dimensions
      // in the low bits of two little-endian 16-bit fields.
      unsigned char buf[10];
      if ((chunkSize < sizeof(buf)) || !readExact(ioDevice, buf, sizeof(buf))) {
        return GENERIC_ERROR;
      }
      if ((buf[3] != 0x9D) || (buf[4] != 0x01) || (buf[5] != 0x2A)) {
        return GENERIC_ERROR;
      }
      width = readLE16(buf + 6) & 0x3FFF;
      height = readLE16(buf + 8) & 0x3FFF;
      haveDimensions = true;
    } else if (std::memcmp(chunkHeader, "VP8L", 4) == 0) {
      // Lossless: 1-byte signature, then width-1 and height-1 as two
      // 14-bit fields packed little-endian.
      unsigned char buf[5];
      if ((chunkSize < sizeof(buf)) || !readExact(ioDevice, buf, sizeof(buf))) {
        return GENERIC_ERROR;
      }
      if (buf[0] != 0x2F) {
        return GENERIC_ERROR;
      }
      const quint32 bits = readLE32(buf + 1);
      width = qint64(bits & 0x3FFF) + 1;
      height = qint64((bits >> 14) & 0x3FFF) + 1;
      haveDimensions = true;
    } else if (std::memcmp(chunkHeader, "VP8X", 4) == 0) {
      // Extended: 4 bytes of flags/reserved, then canvas width-1 and
      // height-1 as 24-bit little-endian fields.
      unsigned char buf[10];
      if ((chunkSize < sizeof(buf)) || !readExact(ioDevice, buf, sizeof(buf))) {
        return GENERIC_ERROR;
      }
      width = qint64(readLE24(buf + 4)) + 1;
      height = qint64(readLE24(buf + 7)) + 1;
      haveDimensions = true;
    } else if (!skipBytes(ioDevice, paddedSize)) {
      return GENERIC_ERROR;
    }
  }

  if (!haveDimensions || (width <= 0) || (height <= 0) || (width > kMaxDimension) || (height > kMaxDimension)) {
    return GENERIC_ERROR;
  }

  // No physical resolution in WebP; one page even for animations.
  out(ImageMetadata(QSize(int(width), int(height)), Dpi()));
  return LOADED;
}  // WebpMetadataLoader::loadMetadata
