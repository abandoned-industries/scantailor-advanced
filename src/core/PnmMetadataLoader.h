// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PNMMETADATALOADER_H_
#define SCANTAILOR_CORE_PNMMETADATALOADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class ImageMetadata;

/**
 * \brief Extracts image metadata from portable anymap (.pbm/.pgm/.ppm) files.
 *
 * Recognizes the P1-P6 magic numbers (ASCII and binary variants) and parses
 * the whitespace- and comment-tolerant header for the dimensions.  PNM
 * stores no physical resolution, so the DPI is left undefined.
 */
class PnmMetadataLoader : public ImageMetadataLoader {
 protected:
  Status loadMetadata(QIODevice& ioDevice, const VirtualFunction<void, const ImageMetadata&>& out) override;
};


#endif
