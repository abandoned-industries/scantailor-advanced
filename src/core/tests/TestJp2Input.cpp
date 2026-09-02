// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// JPEG 2000 (.jp2) input support: the Jp2MetadataLoader must recognize the
// JP2 signature box, report the ihdr dimensions, and leave the DPI undefined
// when no resolution box is present (sips-produced files carry none), so the
// Fix DPI dialog picks the file up.  Decoding itself goes through Qt's qjp2
// plugin via ImageLoader's QImageReader catch-all.
//
// The fixture (fixtures/gradient-97x64.jp2, 97x64 RGB gradient) was produced
// by macOS sips from a synthetic PNG; its box layout is
// jP../ftyp('jp2 ')/jp2h(ihdr,colr)/jp2c with no 'res ' box.
//
// Note: decoding may print harmless "skipping unknown tag type" warnings on
// stderr for sips-produced files; that comes from the JasPer-based plugin and
// is expected.

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

QString jp2FixturePath() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/gradient-97x64.jp2");
}

QString pngFixturePath() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/toned-text-page-92.png");
}

std::vector<ImageMetadata> loadAllMetadata(const QString& path, ImageMetadataLoader::Status& status) {
  std::vector<ImageMetadata> all;
  status = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { all.push_back(metadata); });
  return all;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(Jp2InputTestSuite)

// (a) Metadata: recognized, correct dimensions, exactly one page, and an
// undefined DPI (sips writes no 'res ' box) so Fix DPI takes over.
BOOST_AUTO_TEST_CASE(metadata_loader_reads_jp2_fixture) {
  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(jp2FixturePath(), status);

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), 97);
  BOOST_CHECK_EQUAL(all.front().size().height(), 64);
  BOOST_CHECK(!all.front().isDpiOK());
}

// (b) Full decode through ImageLoader (Qt's qjp2 plugin catch-all).
BOOST_AUTO_TEST_CASE(image_loader_decodes_jp2_fixture) {
  const QImage image = ImageLoader::load(jp2FixturePath(), 0);

  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(image.width(), 97);
  BOOST_CHECK_EQUAL(image.height(), 64);
}

// (c) The metadata chain is undisturbed: a PNG still loads.
BOOST_AUTO_TEST_CASE(png_metadata_chain_undisturbed) {
  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(pngFixturePath(), status);

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_GT(all.front().size().width(), 0);
  BOOST_CHECK_GT(all.front().size().height(), 0);
}

// (d) Truncated .jp2 files are rejected without crashing.
BOOST_AUTO_TEST_CASE(truncated_jp2_is_rejected) {
  QFile source(jp2FixturePath());
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray full = source.readAll();
  BOOST_REQUIRE_GT(full.size(), 100);

  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  // Cut inside the jp2h superbox: the signature and ihdr survive, but the
  // rest of the header is gone.  Must fail cleanly, not crash.
  const QString midTruncated = temp.path() + QStringLiteral("/mid-truncated.jp2");
  {
    QFile out(midTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 100);
  }
  ImageMetadataLoader::Status status;
  std::vector<ImageMetadata> all = loadAllMetadata(midTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::GENERIC_ERROR);
  BOOST_CHECK(all.empty());
  BOOST_CHECK(ImageLoader::load(midTruncated, 0).isNull());

  // Cut inside the signature box itself: not even recognizable.
  const QString tinyTruncated = temp.path() + QStringLiteral("/tiny-truncated.jp2");
  {
    QFile out(tinyTruncated);
    BOOST_REQUIRE(out.open(QIODevice::WriteOnly));
    out.write(full.constData(), 10);
  }
  all = loadAllMetadata(tinyTruncated, status);
  BOOST_CHECK_EQUAL(status, ImageMetadataLoader::FORMAT_NOT_RECOGNIZED);
  BOOST_CHECK(all.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace Tests
