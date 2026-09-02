// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// HEIF/HEIC input support: the HeicMetadataLoader must recognize the ISO
// BMFF 'ftyp' brands, report the primary item's spatial extents reduced by
// the clean aperture crop, and leave the DPI undefined so the Fix DPI
// dialog picks the file up.  Decoding goes through Qt's qmacheif plugin via
// ImageLoader's QImageReader catch-all.
//
// The fixture (fixtures/gradient-101x67.heic, 101x67 RGB gradient) was
// produced by macOS sips from a synthetic PNG.  HEVC pads the coded size to
// even dimensions, so its 'ispe' box says 102x68 while the 'clap' box crops
// back to the true 101x67 - which is exactly what Qt decodes.  The metadata
// dimensions MUST match the decoded dimensions or thumbnails and page
// geometry would silently disagree with the pixels.

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

QString heicFixturePath() {
  return QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/core/tests/fixtures/gradient-101x67.heic");
}

std::vector<ImageMetadata> loadAllMetadata(const QString& path, ImageMetadataLoader::Status& status) {
  std::vector<ImageMetadata> all;
  status = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { all.push_back(metadata); });
  return all;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(HeicInputTestSuite)

// (a) Metadata: recognized, clap-cropped dimensions, exactly one page, and
// an undefined DPI so Fix DPI takes over.
BOOST_AUTO_TEST_CASE(metadata_loader_reads_heic_fixture) {
  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(heicFixturePath(), status);

  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), 101);
  BOOST_CHECK_EQUAL(all.front().size().height(), 67);
  BOOST_CHECK(!all.front().isDpiOK());
}

// (b) Full decode through ImageLoader (Qt's qmacheif plugin catch-all),
// plus the ispe-choice check: metadata dims == decoded dims.
BOOST_AUTO_TEST_CASE(image_loader_decodes_heic_fixture_and_matches_metadata) {
  const QImage image = ImageLoader::load(heicFixturePath(), 0);

  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(image.width(), 101);
  BOOST_CHECK_EQUAL(image.height(), 67);

  ImageMetadataLoader::Status status;
  const std::vector<ImageMetadata> all = loadAllMetadata(heicFixturePath(), status);
  BOOST_REQUIRE_EQUAL(status, ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(all.size(), 1u);
  BOOST_CHECK_EQUAL(all.front().size().width(), image.width());
  BOOST_CHECK_EQUAL(all.front().size().height(), image.height());
}

// (c) Truncated .heic files are rejected without crashing.
BOOST_AUTO_TEST_CASE(truncated_heic_is_rejected) {
  QFile source(heicFixturePath());
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray full = source.readAll();
  BOOST_REQUIRE_GT(full.size(), 100);

  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  // Cut inside the 'meta' box: the brand check passes, but the metadata
  // structure is incomplete.  Must fail cleanly, not crash.
  const QString midTruncated = temp.path() + QStringLiteral("/mid-truncated.heic");
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

  // Cut inside the 'ftyp' box itself: not even recognizable.
  const QString tinyTruncated = temp.path() + QStringLiteral("/tiny-truncated.heic");
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
