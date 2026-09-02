// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// WebP input support: the WebpMetadataLoader must recognize the RIFF/WEBP
// container, read the dimensions from lossy ('VP8 ') and lossless ('VP8L')
// image chunks, and leave the DPI undefined (WebP stores no physical
// resolution) so the Fix DPI dialog picks the file up.  Decoding goes
// through Qt's qwebp plugin via ImageLoader's QImageReader catch-all.
//
// Fixtures: gradient-89x60.webp (lossy VP8) and
// gradient-41x27-lossless.webp (lossless VP8L), both written by
// QImage::save.  The 'VP8X' extended-container path is exercised by a
// synthetic in-test header, since neither Qt nor sips writes VP8X for
// still images.

#include <ImageLoader.h>
#include <ImageMetadata.h>
#include <ImageMetadataLoader.h>

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QString>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace Tests {
namespace {

QString fixturePath(const char* name) {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/") + QLatin1String(name);
}

std::vector<ImageMetadata> loadAllMetadata(const QString& path, ImageMetadataLoader::Status& status) {
  std::vector<ImageMetadata> all;
  status = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { all.push_back(metadata); });
  return all;
}

void checkMetadataAndDecode(const QString& path, int width, int height) {
  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(path, status);

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), width);
  BOOST_CHECK_EQUAL(all.front().size().height(), height);
  BOOST_CHECK(!all.front().isDpiOK());

  const QImage image = ImageLoader::load(path, 0);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(image.width(), width);
  BOOST_CHECK_EQUAL(image.height(), height);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(WebpInputTestSuite)

// (a)+(b) Metadata and decode for a lossy (VP8) file.
BOOST_AUTO_TEST_CASE(lossy_vp8_webp_loads) {
  checkMetadataAndDecode(fixturePath("gradient-89x60.webp"), 89, 60);
}

// (a)+(b) Metadata and decode for a lossless (VP8L) file.
BOOST_AUTO_TEST_CASE(lossless_vp8l_webp_loads) {
  checkMetadataAndDecode(fixturePath("gradient-41x27-lossless.webp"), 41, 27);
}

// (a) Metadata for a synthetic extended (VP8X) header: canvas 300x200
// stored as 24-bit little-endian width-1/height-1 fields.
BOOST_AUTO_TEST_CASE(extended_vp8x_header_parses) {
  QByteArray data;
  data.append("RIFF");
  data.append("\x16\x00\x00\x00", 4);  // RIFF payload size: 22.
  data.append("WEBP");
  data.append("VP8X");
  data.append("\x0a\x00\x00\x00", 4);  // Chunk size: 10.
  data.append(4, '\x00');              // Flags + reserved.
  data.append("\x2b\x01\x00", 3);      // Canvas width - 1 = 299.
  data.append("\xc7\x00\x00", 3);      // Canvas height - 1 = 199.

  QBuffer buffer(&data);
  BOOST_REQUIRE(buffer.open(QIODevice::ReadOnly));
  std::vector<ImageMetadata> all;
  const ImageMetadataLoader::Status status
      = ImageMetadataLoader::load(buffer, [&](const ImageMetadata& metadata) { all.push_back(metadata); });

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), 300);
  BOOST_CHECK_EQUAL(all.front().size().height(), 200);
}

// (c) Truncated .webp files are rejected without crashing.
BOOST_AUTO_TEST_CASE(truncated_webp_is_rejected) {
  QFile source(fixturePath("gradient-89x60.webp"));
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray full = source.readAll();
  BOOST_REQUIRE_GT(full.size(), 30);

  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  // Cut inside the VP8 chunk header: the container signature survives,
  // the dimensions are gone.
  const QString midTruncated = temp.path() + QStringLiteral("/mid-truncated.webp");
  {
    QFile out(midTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 18);
  }
  ImageMetadataLoader::Status status;
  std::vector<ImageMetadata> all = loadAllMetadata(midTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::GENERIC_ERROR);
  BOOST_CHECK(all.empty());
  BOOST_CHECK(ImageLoader::load(midTruncated, 0).isNull());

  // Cut inside the RIFF header itself: not even recognizable.
  const QString tinyTruncated = temp.path() + QStringLiteral("/tiny-truncated.webp");
  {
    QFile out(tinyTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 8);
  }
  all = loadAllMetadata(tinyTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::FORMAT_NOT_RECOGNIZED);
  BOOST_CHECK(all.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace Tests
