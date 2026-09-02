// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// GIF input support: the GifMetadataLoader must recognize the GIF87a/GIF89a
// signature, report the logical screen descriptor dimensions as one page,
// and leave the DPI undefined (GIF stores no physical resolution) so the
// Fix DPI dialog picks the file up.  Decoding goes through Qt's qgif plugin
// via ImageLoader's QImageReader catch-all.
//
// The fixture (fixtures/gradient-83x57.gif, 83x57 RGB gradient) was produced
// by macOS sips from a synthetic PNG; sips writes a GIF87a header.

#include <ImageLoader.h>
#include <ImageMetadata.h>
#include <ImageMetadataLoader.h>

#include <QFile>
#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace Tests {
namespace {

QString gifFixturePath() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/gradient-83x57.gif");
}

std::vector<ImageMetadata> loadAllMetadata(const QString& path, ImageMetadataLoader::Status& status) {
  std::vector<ImageMetadata> all;
  status = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { all.push_back(metadata); });
  return all;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(GifInputTestSuite)

// (a) Metadata: recognized, correct dimensions, exactly one page, and an
// undefined DPI so Fix DPI takes over.
BOOST_AUTO_TEST_CASE(metadata_loader_reads_gif_fixture) {
  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(gifFixturePath(), status);

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), 83);
  BOOST_CHECK_EQUAL(all.front().size().height(), 57);
  BOOST_CHECK(!all.front().isDpiOK());
}

// (b) Full decode through ImageLoader (Qt's qgif plugin catch-all).
BOOST_AUTO_TEST_CASE(image_loader_decodes_gif_fixture) {
  const QImage image = ImageLoader::load(gifFixturePath(), 0);

  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(image.width(), 83);
  BOOST_CHECK_EQUAL(image.height(), 57);
}

// (c) Truncated .gif files are rejected without crashing.
BOOST_AUTO_TEST_CASE(truncated_gif_is_rejected) {
  QFile source(gifFixturePath());
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray full = source.readAll();
  BOOST_REQUIRE_GT(full.size(), 13);

  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  // Cut inside the logical screen descriptor: the signature survives, the
  // dimensions are gone.
  const QString midTruncated = temp.path() + QStringLiteral("/mid-truncated.gif");
  {
    QFile out(midTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 8);
  }
  ImageMetadataLoader::Status status;
  std::vector<ImageMetadata> all = loadAllMetadata(midTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::GENERIC_ERROR);
  BOOST_CHECK(all.empty());
  BOOST_CHECK(ImageLoader::load(midTruncated, 0).isNull());

  // Cut inside the signature itself: not even recognizable.
  const QString tinyTruncated = temp.path() + QStringLiteral("/tiny-truncated.gif");
  {
    QFile out(tinyTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 4);
  }
  all = loadAllMetadata(tinyTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::FORMAT_NOT_RECOGNIZED);
  BOOST_CHECK(all.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace Tests
