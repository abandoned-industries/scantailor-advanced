// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Portable anymap (.pbm/.pgm/.ppm) input support: the PnmMetadataLoader must
// recognize the P1-P6 magics, parse the whitespace/comment-tolerant header
// dimensions, and leave the DPI undefined (PNM stores no physical
// resolution) so the Fix DPI dialog picks the file up.  Decoding goes
// through Qt's built-in PNM handler via ImageLoader's QImageReader
// catch-all.
//
// Fixtures: gradient-79x53.ppm (binary P6) and gradient-61x38.pgm (binary
// P5) were written by QImage::save; gradient-ascii-31x19.ppm (ASCII P3 with
// a '#' comment line) was written by a small script.

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

BOOST_AUTO_TEST_SUITE(PnmInputTestSuite)

// (a)+(b) Metadata and decode for a binary P6 .ppm.
BOOST_AUTO_TEST_CASE(binary_p6_ppm_loads) {
  checkMetadataAndDecode(fixturePath("gradient-79x53.ppm"), 79, 53);
}

// (a)+(b) Metadata and decode for a binary P5 .pgm.
BOOST_AUTO_TEST_CASE(binary_p5_pgm_loads) {
  checkMetadataAndDecode(fixturePath("gradient-61x38.pgm"), 61, 38);
}

// (a)+(b) Metadata and decode for an ASCII P3 .ppm with a comment line.
BOOST_AUTO_TEST_CASE(ascii_p3_ppm_loads) {
  checkMetadataAndDecode(fixturePath("gradient-ascii-31x19.ppm"), 31, 19);
}

// (c) Truncated PNM files are rejected without crashing.
BOOST_AUTO_TEST_CASE(truncated_pnm_is_rejected) {
  QFile source(fixturePath("gradient-79x53.ppm"));
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray full = source.readAll();
  BOOST_REQUIRE_GT(full.size(), 15);

  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  // Cut right after the magic: recognized, but the dimensions are gone.
  const QString midTruncated = temp.path() + QStringLiteral("/mid-truncated.ppm");
  {
    QFile out(midTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 3);
  }
  ImageMetadataLoader::Status status;
  std::vector<ImageMetadata> all = loadAllMetadata(midTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::GENERIC_ERROR);
  BOOST_CHECK(all.empty());
  BOOST_CHECK(ImageLoader::load(midTruncated, 0).isNull());

  // Cut inside the magic itself: not even recognizable.
  const QString tinyTruncated = temp.path() + QStringLiteral("/tiny-truncated.ppm");
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
