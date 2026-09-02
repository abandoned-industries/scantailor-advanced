// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_GIFMETADATALOADER_H_
#define SCANTAILOR_CORE_GIFMETADATALOADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class ImageMetadata;

/**
 * \brief Extracts image metadata from GIF files.
 *
 * Reads the logical screen descriptor of GIF87a/GIF89a files.  GIF stores
 * no physical resolution, so the DPI is left undefined and the Fix DPI
 * dialog picks the file up.  Exactly one page is reported even for animated
 * GIFs, matching ImageLoader, which decodes only the first frame.
 */
class GifMetadataLoader : public ImageMetadataLoader {
 protected:
  Status loadMetadata(QIODevice& ioDevice, const VirtualFunction<void, const ImageMetadata&>& out) override;
};


#endif
