// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_JP2METADATALOADER_H_
#define SCANTAILOR_CORE_JP2METADATALOADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class ImageMetadata;

/**
 * \brief Extracts image metadata from JPEG 2000 (.jp2) files.
 *
 * Only the JP2 container format is recognized (the 'jp2 ' brand);
 * extended JPX files and raw codestreams are not.
 */
class Jp2MetadataLoader : public ImageMetadataLoader {
 protected:
  Status loadMetadata(QIODevice& ioDevice, const VirtualFunction<void, const ImageMetadata&>& out) override;
};


#endif
