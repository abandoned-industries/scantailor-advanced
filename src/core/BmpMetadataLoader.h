// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_BMPMETADATALOADER_H_
#define SCANTAILOR_CORE_BMPMETADATALOADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class ImageMetadata;

/**
 * \brief Extracts image metadata from Windows bitmap (.bmp) files.
 *
 * Handles both BITMAPCOREHEADER (12 bytes) and BITMAPINFOHEADER-compatible
 * (40+ bytes) DIB headers.  The biXPelsPerMeter/biYPelsPerMeter fields are
 * converted to DPI when present and nonzero.
 */
class BmpMetadataLoader : public ImageMetadataLoader {
 protected:
  Status loadMetadata(QIODevice& ioDevice, const VirtualFunction<void, const ImageMetadata&>& out) override;
};


#endif
