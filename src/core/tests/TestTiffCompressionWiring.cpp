// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Characterization test for the TIFF compression wiring (Stage 1.5; audit F2,
// prereqs Part 2c; QW4 landed).
//
// Pins:
//  - TiffWriter reads compression from the QSettings keys
//    settings/bw_compression and settings/color_compression via
//    ApplicationSettings, defaulting to CCITT G4 (4) and LZW (5).
//  - Changing those keys changes the written file.
//  - Since QW4, the finalize control is LIVE: finalize::Settings::
//    setTiffCompression maps the UI enum explicitly (LZW -> 5, Deflate -> 8)
//    onto settings/color_compression, which TiffWriter reads; the bitonal key
//    (CCITT G4) is deliberately untouched by the control. The finalize
//    Settings constructor reflects the persisted key back into the UI value
//    without writing, so untouched configurations keep their behavior.
//  - The old MainWindow-side leg (OutputFileNameGenerator::setTiffCompression)
//    remains a dead end.
//  - The two enum vocabularies (finalize 0/1 vs libtiff 4/5/8) do not overlap.

#include <ApplicationSettings.h>
#include <OutputFileNameGenerator.h>
#include <TiffWriter.h>
#include <tiff.h>
#include <tiffio.h>

#include <QCoreApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#include "filters/finalize/Settings.h"

namespace Tests {
namespace {

// Sandbox QSettings for the whole test binary BEFORE any test (and before the
// ApplicationSettings singleton, whose QSettings member binds at first use)
// can touch the real user configuration.
struct QSettingsSandbox {
  QSettingsSandbox() {
    static QTemporaryDir dir;  // leaked until process exit; keeps path alive
    QCoreApplication::setOrganizationName(QStringLiteral("ScanTailorSpectreTests"));
    QCoreApplication::setApplicationName(QStringLiteral("core_tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
  }
};
BOOST_GLOBAL_FIXTURE(QSettingsSandbox);

QImage makeMonoImage() {
  QImage image(64, 64, QImage::Format_Mono);
  image.fill(1);
  for (int x = 0; x < 64; ++x) {
    image.setPixel(x, 32, 0);
  }
  return image;
}

QImage makeRgbImage() {
  QImage image(64, 64, QImage::Format_RGB32);
  image.fill(qRgb(200, 100, 50));
  return image;
}

uint16_t readCompressionTag(const QString& filePath) {
  TIFF* tif = TIFFOpen(filePath.toLocal8Bit().constData(), "r");
  BOOST_REQUIRE(tif != nullptr);
  uint16_t compression = 0;
  BOOST_REQUIRE(TIFFGetField(tif, TIFFTAG_COMPRESSION, &compression) == 1);
  TIFFClose(tif);
  return compression;
}

uint16_t writeAndReadCompression(const QImage& image, const QString& dir, const char* name) {
  const QString path = dir + QLatin1Char('/') + QLatin1String(name);
  BOOST_REQUIRE(TiffWriter::writeImage(path, image));
  return readCompressionTag(path);
}

struct SettingsKeyRestorer {
  ~SettingsKeyRestorer() {
    ApplicationSettings::getInstance().setTiffBwCompression(COMPRESSION_CCITTFAX4);
    ApplicationSettings::getInstance().setTiffColorCompression(COMPRESSION_LZW);
  }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(TiffCompressionWiringTestSuite)

// Defaults: bitonal -> CCITT G4 (4), color -> LZW (5); values are raw libtiff codes.
BOOST_AUTO_TEST_CASE(defaults_pinned) {
  BOOST_CHECK_EQUAL(ApplicationSettings::getInstance().getTiffBwCompression(), COMPRESSION_CCITTFAX4);
  BOOST_CHECK_EQUAL(ApplicationSettings::getInstance().getTiffColorCompression(), COMPRESSION_LZW);

  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeMonoImage(), dir.path(), "mono.tif"), COMPRESSION_CCITTFAX4);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeRgbImage(), dir.path(), "rgb.tif"), COMPRESSION_LZW);
}

// The hand-editable QSettings keys are live: changing them changes the file.
BOOST_AUTO_TEST_CASE(qsettings_keys_are_live) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  SettingsKeyRestorer restore;

  ApplicationSettings::getInstance().setTiffBwCompression(COMPRESSION_LZW);
  ApplicationSettings::getInstance().setTiffColorCompression(COMPRESSION_ADOBE_DEFLATE);

  BOOST_CHECK_EQUAL(writeAndReadCompression(makeMonoImage(), dir.path(), "mono.tif"), COMPRESSION_LZW);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeRgbImage(), dir.path(), "rgb.tif"), COMPRESSION_ADOBE_DEFLATE);
}

// QW4: the finalize control is live end-to-end. Setting the finalize value
// reaches the file TiffWriter writes, with the explicit enum mapping
// (LZW -> COMPRESSION_LZW, Deflate -> COMPRESSION_ADOBE_DEFLATE), while the
// bitonal compression stays CCITT G4 (the control governs the
// continuous-tone codec only).
BOOST_AUTO_TEST_CASE(finalize_choice_reaches_tiff_writer) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  SettingsKeyRestorer restore;

  finalize::Settings finalizeSettings;
  finalizeSettings.setTiffCompression(finalize::TiffCompression::Deflate);
  BOOST_CHECK(finalizeSettings.tiffCompression() == finalize::TiffCompression::Deflate);
  BOOST_CHECK_EQUAL(ApplicationSettings::getInstance().getTiffColorCompression(), COMPRESSION_ADOBE_DEFLATE);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeRgbImage(), dir.path(), "rgb-deflate.tif"),
                    COMPRESSION_ADOBE_DEFLATE);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeMonoImage(), dir.path(), "mono-deflate.tif"),
                    COMPRESSION_CCITTFAX4);

  finalizeSettings.setTiffCompression(finalize::TiffCompression::LZW);
  BOOST_CHECK_EQUAL(ApplicationSettings::getInstance().getTiffColorCompression(), COMPRESSION_LZW);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeRgbImage(), dir.path(), "rgb-lzw.tif"), COMPRESSION_LZW);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeMonoImage(), dir.path(), "mono-lzw.tif"), COMPRESSION_CCITTFAX4);
}

// QW4: a fresh finalize::Settings reflects the persisted key back into the
// UI-facing value — and only reflects: constructing it never writes the key,
// so hand-edited/legacy values survive until the user touches the control.
BOOST_AUTO_TEST_CASE(finalize_settings_reflects_persisted_choice) {
  SettingsKeyRestorer restore;

  ApplicationSettings::getInstance().setTiffColorCompression(COMPRESSION_ADOBE_DEFLATE);
  BOOST_CHECK(finalize::Settings().tiffCompression() == finalize::TiffCompression::Deflate);
  BOOST_CHECK_EQUAL(ApplicationSettings::getInstance().getTiffColorCompression(), COMPRESSION_ADOBE_DEFLATE);

  ApplicationSettings::getInstance().setTiffColorCompression(COMPRESSION_LZW);
  BOOST_CHECK(finalize::Settings().tiffCompression() == finalize::TiffCompression::LZW);

  // A hand-edited code outside the control's vocabulary: default display,
  // key preserved untouched.
  ApplicationSettings::getInstance().setTiffColorCompression(COMPRESSION_CCITTFAX4);
  BOOST_CHECK(finalize::Settings().tiffCompression() == finalize::TiffCompression::LZW);
  BOOST_CHECK_EQUAL(ApplicationSettings::getInstance().getTiffColorCompression(), COMPRESSION_CCITTFAX4);
}

// The old MainWindow-side leg is still a dead end: OutputFileNameGenerator's
// value goes nowhere (kept pinned so a future cleanup knows it is inert).
BOOST_AUTO_TEST_CASE(output_file_name_generator_leg_still_inert) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  SettingsKeyRestorer restore;

  OutputFileNameGenerator gen;
  gen.setTiffCompression(OutputTiffCompression::Deflate);
  BOOST_CHECK(gen.tiffCompression() == OutputTiffCompression::Deflate);

  BOOST_CHECK_EQUAL(writeAndReadCompression(makeMonoImage(), dir.path(), "mono.tif"), COMPRESSION_CCITTFAX4);
  BOOST_CHECK_EQUAL(writeAndReadCompression(makeRgbImage(), dir.path(), "rgb.tif"), COMPRESSION_LZW);
}

// The two vocabularies do not overlap on a single value: a naive "sync" that
// wrote the finalize enum's raw int into the QSettings key would hand libtiff
// COMPRESSION_NONE (1) or 0. QW4's fix must translate explicitly.
BOOST_AUTO_TEST_CASE(enum_vocabulary_mismatch_pinned) {
  BOOST_CHECK_EQUAL(static_cast<int>(finalize::TiffCompression::LZW), 0);
  BOOST_CHECK_EQUAL(static_cast<int>(finalize::TiffCompression::Deflate), 1);
  BOOST_CHECK_EQUAL(static_cast<int>(OutputTiffCompression::LZW), 0);
  BOOST_CHECK_EQUAL(static_cast<int>(OutputTiffCompression::Deflate), 1);
  BOOST_CHECK_EQUAL(COMPRESSION_NONE, 1);
  BOOST_CHECK_EQUAL(COMPRESSION_CCITTFAX4, 4);
  BOOST_CHECK_EQUAL(COMPRESSION_LZW, 5);
  BOOST_CHECK_EQUAL(COMPRESSION_ADOBE_DEFLATE, 8);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
