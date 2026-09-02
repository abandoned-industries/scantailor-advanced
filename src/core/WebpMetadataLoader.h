// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_WEBPMETADATALOADER_H_
#define SCANTAILOR_CORE_WEBPMETADATALOADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class ImageMetadata;

/**
 * \brief Extracts image metadata from WebP files.
 *
 * Recognizes the RIFF/WEBP container and reads the dimensions from the
 * first image chunk: 'VP8 ' (lossy), 'VP8L' (lossless) or 'VP8X'
 * (extended, including animations).  WebP stores no physical resolution,
 * so the DPI is left undefined.  Exactly one page is reported even for
 * animated files, matching ImageLoader, which decodes only the first frame.
 */
class WebpMetadataLoader : public ImageMetadataLoader {
 protected:
  Status loadMetadata(QIODevice& ioDevice, const VirtualFunction<void, const ImageMetadata&>& out) override;
};


#endif
