// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageMetadataLoader.h"

#include <QFile>
#include <QIODevice>
#include <QString>

#include "BmpMetadataLoader.h"
#include "GifMetadataLoader.h"
#include "HeicMetadataLoader.h"
#include "ImageMetadata.h"
#include "Jp2MetadataLoader.h"
#include "JpegMetadataLoader.h"
#include "PdfMetadataLoader.h"
#include "PngMetadataLoader.h"
#include "PnmMetadataLoader.h"
#include "TiffMetadataLoader.h"
#include "WebpMetadataLoader.h"

ImageMetadataLoader::LoaderList ImageMetadataLoader::m_sLoaders;

void ImageMetadataLoader::registerLoader(std::shared_ptr<ImageMetadataLoader> loader) {
  m_sLoaders.push_back(std::move(loader));
}

ImageMetadataLoader::StaticInit::StaticInit() {
  registerLoader(std::make_shared<JpegMetadataLoader>());
  registerLoader(std::make_shared<PngMetadataLoader>());
  registerLoader(std::make_shared<TiffMetadataLoader>());
  registerLoader(std::make_shared<PdfMetadataLoader>());
  // JP2's signature box cannot collide with any of the magics above.
  registerLoader(std::make_shared<Jp2MetadataLoader>());
  // The magics below ('BM', 'GIF8', 'P1'-'P6', 'RIFF'+'WEBP', 'ftyp' with a
  // HEIF brand) are mutually exclusive with everything above,
  // and each loader leaves the device untouched on a mismatch.
  registerLoader(std::make_shared<BmpMetadataLoader>());
  registerLoader(std::make_shared<GifMetadataLoader>());
  registerLoader(std::make_shared<PnmMetadataLoader>());
  registerLoader(std::make_shared<WebpMetadataLoader>());
  registerLoader(std::make_shared<HeicMetadataLoader>());
}

ImageMetadataLoader::StaticInit ImageMetadataLoader::m_staticInit;

ImageMetadataLoader::Status ImageMetadataLoader::loadImpl(QIODevice& ioDevice,
                                                          const VirtualFunction<void, const ImageMetadata&>& out) {
  auto it(m_sLoaders.begin());
  const auto end(m_sLoaders.end());
  for (; it != end; ++it) {
    const Status status = (*it)->loadMetadata(ioDevice, out);
    if (status != FORMAT_NOT_RECOGNIZED) {
      return status;
    }
  }
  return FORMAT_NOT_RECOGNIZED;
}

ImageMetadataLoader::Status ImageMetadataLoader::loadImpl(const QString& filePath,
                                                          const VirtualFunction<void, const ImageMetadata&>& out) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return GENERIC_ERROR;
  }
  return loadImpl(file, out);
}