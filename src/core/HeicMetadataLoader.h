// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_HEICMETADATALOADER_H_
#define SCANTAILOR_CORE_HEICMETADATALOADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class ImageMetadata;

/**
 * \brief Extracts image metadata from HEIF/HEIC (.heic/.heif) files.
 *
 * Recognizes ISO BMFF files whose 'ftyp' brand (major or compatible) is one
 * of heic/heix/mif1/msf1/heif, then reads the primary item's spatial
 * extents ('ispe') from the 'meta' box, reduced by its clean aperture
 * ('clap') crop when present, matching the dimensions decoders actually
 * produce.  HEIF carries no plain physical resolution (only EXIF, which is
 * not parsed), so the DPI is left undefined.
 */
class HeicMetadataLoader : public ImageMetadataLoader {
 protected:
  Status loadMetadata(QIODevice& ioDevice, const VirtualFunction<void, const ImageMetadata&>& out) override;
};


#endif
