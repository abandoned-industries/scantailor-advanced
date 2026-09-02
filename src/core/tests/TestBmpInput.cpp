// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Windows bitmap (.bmp) input support: the BmpMetadataLoader must recognize
// the 'BM' magic, report the BITMAPINFOHEADER dimensions, and convert the
// biXPelsPerMeter/biYPelsPerMeter fields to DPI.  Decoding goes through Qt's
// built-in BMP handler via ImageLoader's QImageReader catch-all.
//
// The fixture (fixtures/gradient-97x64.bmp, 97x64 RGB gradient) was written
// by QImage::save with dotsPerMeter 5906 (150 DPI) in both directions.

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

QString bmpFixturePath() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/gradient-97x64.bmp");
}

std::vector<ImageMetadata> loadAllMetadata(const QString& path, ImageMetadataLoader::Status& status) {
  std::vector<ImageMetadata> all;
  status = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { all.push_back(metadata); });
  return all;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(BmpInputTestSuite)

// (a) Metadata: recognized, correct dimensions, exactly one page, and the
// pels-per-meter fields converted to a defined 150x150 DPI.
BOOST_AUTO_TEST_CASE(metadata_loader_reads_bmp_fixture) {
  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(bmpFixturePath(), status);

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), 97);
  BOOST_CHECK_EQUAL(all.front().size().height(), 64);
  BOOST_CHECK(all.front().isDpiOK());
  BOOST_CHECK_EQUAL(all.front().dpi().horizontal(), 150);
  BOOST_CHECK_EQUAL(all.front().dpi().vertical(), 150);
}

// (b) Full decode through ImageLoader (Qt's built-in BMP handler catch-all).
BOOST_AUTO_TEST_CASE(image_loader_decodes_bmp_fixture) {
  const QImage image = ImageLoader::load(bmpFixturePath(), 0);

  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(image.width(), 97);
  BOOST_CHECK_EQUAL(image.height(), 64);
}

// (c) Truncated .bmp files are rejected without crashing.
BOOST_AUTO_TEST_CASE(truncated_bmp_is_rejected) {
  QFile source(bmpFixturePath());
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray full = source.readAll();
  BOOST_REQUIRE_GT(full.size(), 54);

  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  // Cut inside the DIB header: the magic survives, the dimensions are gone.
  const QString midTruncated = temp.path() + QStringLiteral("/mid-truncated.bmp");
  {
    QFile out(midTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 20);
  }
  ImageMetadataLoader::Status status;
  std::vector<ImageMetadata> all = loadAllMetadata(midTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::GENERIC_ERROR);
  BOOST_CHECK(all.empty());
  BOOST_CHECK(ImageLoader::load(midTruncated, 0).isNull());

  // Cut inside the magic itself: not even recognizable.
  const QString tinyTruncated = temp.path() + QStringLiteral("/tiny-truncated.bmp");
  {
    QFile out(tinyTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 1);
  }
  all = loadAllMetadata(tinyTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::FORMAT_NOT_RECOGNIZED);
  BOOST_CHECK(all.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace Tests
