// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "GifMetadataLoader.h"

#include <QIODevice>
#include <cstring>

#include "ImageMetadata.h"

namespace {
quint16 readLE16(const unsigned char* p) {
  return quint16(quint16(p[0]) | (quint16(p[1]) << 8));
}
}  // namespace

ImageMetadataLoader::Status GifMetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                            const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  // Header (6 bytes) plus the first 4 bytes of the logical screen
  // descriptor.  Everything is peeked, so the device is left untouched.
  unsigned char header[10];
  const qint64 peeked = ioDevice.peek((char*) header, sizeof(header));
  if (peeked < 6) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if ((std::memcmp(header, "GIF87a", 6) != 0) && (std::memcmp(header, "GIF89a", 6) != 0)) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if (peeked < qint64(sizeof(header))) {
    // The signature matched but the logical screen descriptor is cut off.
    return GENERIC_ERROR;
  }

  const quint16 width = readLE16(header + 6);
  const quint16 height = readLE16(header + 8);
  if ((width == 0) || (height == 0)) {
    return GENERIC_ERROR;
  }

  // No physical resolution in GIF; one page even for animations.
  out(ImageMetadata(QSize(width, height), Dpi()));
  return LOADED;
}  // GifMetadataLoader::loadMetadata
