#include <boost/test/unit_test.hpp>
#include <QDomDocument>
#include <QFile>

#include "ImageId.h"
#include "PageId.h"
#include "filters/finalize/Settings.h"
#include "filters/finalize/ThumbnailColorModeFilter.h"
#include "filters/output/ColorParams.h"
#include "LeptonicaDetector.h"

BOOST_AUTO_TEST_SUITE(AutoColorModePolicyTestSuite)

BOOST_AUTO_TEST_CASE(never_color_maps_only_color_to_grayscale) {
  using finalize::AutoColorModePolicy;
  using finalize::ColorMode;
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Color, AutoColorModePolicy::NeverColor)
              == ColorMode::Grayscale);
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::BlackAndWhite, AutoColorModePolicy::NeverColor)
              == ColorMode::BlackAndWhite);
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Grayscale, AutoColorModePolicy::NeverColor)
              == ColorMode::Grayscale);
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Mixed, AutoColorModePolicy::NeverColor)
              == ColorMode::Grayscale);
}

BOOST_AUTO_TEST_CASE(force_black_and_white_maps_every_verdict) {
  using finalize::AutoColorModePolicy;
  using finalize::ColorMode;
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Color, AutoColorModePolicy::ForceBlackAndWhite)
              == ColorMode::BlackAndWhite);
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Grayscale, AutoColorModePolicy::ForceBlackAndWhite)
              == ColorMode::BlackAndWhite);
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Mixed, AutoColorModePolicy::ForceBlackAndWhite)
              == ColorMode::BlackAndWhite);
}

BOOST_AUTO_TEST_CASE(best_guess_preserves_mixed_verdict) {
  using finalize::AutoColorModePolicy;
  using finalize::ColorMode;
  BOOST_CHECK(applyAutoColorModePolicy(ColorMode::Mixed, AutoColorModePolicy::BestGuess)
              == ColorMode::Mixed);
}

BOOST_AUTO_TEST_CASE(thumbnail_color_mode_filter_keeps_mixed_independent) {
  using finalize::ColorMode;
  using finalize::ThumbnailColorModeFilter;

  const ThumbnailColorModeFilter mixedOnly(false, false, true, false);
  BOOST_CHECK(!mixedOnly.includes(ColorMode::BlackAndWhite));
  BOOST_CHECK(!mixedOnly.includes(ColorMode::Grayscale));
  BOOST_CHECK(mixedOnly.includes(ColorMode::Mixed));
  BOOST_CHECK(!mixedOnly.includes(ColorMode::Color));
  BOOST_CHECK(!mixedOnly.includesAll());

  const ThumbnailColorModeFilter colorOnly(false, false, false, true);
  BOOST_CHECK(!colorOnly.includes(ColorMode::Mixed));
  BOOST_CHECK(colorOnly.includes(ColorMode::Color));

  const ThumbnailColorModeFilter allModes(true, true, true, true);
  BOOST_CHECK(allModes.includesAll());
}

BOOST_AUTO_TEST_CASE(main_window_exposes_checked_mixed_thumbnail_filter) {
  QFile file(QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/src/app/MainWindow.ui"));
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));

  QDomDocument document;
  BOOST_REQUIRE(document.setContent(&file));
  const QDomNodeList widgets = document.elementsByTagName(QStringLiteral("widget"));
  QDomElement mixedButton;
  int bwIndex = -1;
  int grayIndex = -1;
  int mixedIndex = -1;
  int colorIndex = -1;
  for (int i = 0; i < widgets.size(); ++i) {
    const QDomElement widget = widgets.at(i).toElement();
    const QString name = widget.attribute(QStringLiteral("name"));
    if (name == QStringLiteral("filterBwBtn")) {
      bwIndex = i;
    } else if (name == QStringLiteral("filterGrayBtn")) {
      grayIndex = i;
    } else if (name == QStringLiteral("filterMixedBtn")) {
      mixedButton = widget;
      mixedIndex = i;
    } else if (name == QStringLiteral("filterColorBtn")) {
      colorIndex = i;
    }
  }
  BOOST_REQUIRE(!mixedButton.isNull());
  BOOST_CHECK(bwIndex < grayIndex);
  BOOST_CHECK(grayIndex < mixedIndex);
  BOOST_CHECK(mixedIndex < colorIndex);

  auto propertyText = [&mixedButton](const QString& name) {
    for (QDomElement property = mixedButton.firstChildElement(QStringLiteral("property"));
         !property.isNull(); property = property.nextSiblingElement(QStringLiteral("property"))) {
      if (property.attribute(QStringLiteral("name")) == name) {
        return property.firstChildElement().text();
      }
    }
    return QString();
  };

  BOOST_CHECK(propertyText(QStringLiteral("text")) == QStringLiteral("M"));
  BOOST_CHECK(propertyText(QStringLiteral("toolTip"))
              == QStringLiteral("Show/hide Mixed pages in thumbnails"));
  BOOST_CHECK(propertyText(QStringLiteral("checkable")) == QStringLiteral("true"));
  BOOST_CHECK(propertyText(QStringLiteral("checked")) == QStringLiteral("true"));
}

BOOST_AUTO_TEST_CASE(finalize_mixed_mode_round_trips_through_xml) {
  finalize::Params params;
  params.setColorMode(finalize::ColorMode::Mixed);
  params.setColorModeDetected(true);

  QDomDocument document;
  const finalize::Params restored(params.toXml(document, QStringLiteral("params")));
  BOOST_CHECK(restored.colorMode() == finalize::ColorMode::Mixed);
  BOOST_CHECK(restored.isColorModeDetected());
}

BOOST_AUTO_TEST_CASE(output_mixed_mode_round_trips_through_xml) {
  output::ColorParams params;
  params.setColorMode(output::MIXED);
  params.setColorModeUserSet(true);

  QDomDocument document;
  const output::ColorParams restored(params.toXml(document, QStringLiteral("color-params")));
  BOOST_CHECK(restored.colorMode() == output::MIXED);
  BOOST_CHECK(restored.isColorModeUserSet());
}

BOOST_AUTO_TEST_CASE(best_guess_recovers_the_raw_color_verdict) {
  using finalize::AutoColorModePolicy;
  using finalize::ColorMode;
  const ColorMode rawVerdict = ColorMode::Color;
  BOOST_CHECK(applyAutoColorModePolicy(rawVerdict, AutoColorModePolicy::NeverColor)
              == ColorMode::Grayscale);
  BOOST_CHECK(applyAutoColorModePolicy(rawVerdict, AutoColorModePolicy::BestGuess)
              == ColorMode::Color);
}

BOOST_AUTO_TEST_CASE(force_preset_provenance_is_distinct_from_manual_choice) {
  output::ColorParams params;
  params.setColorMode(output::BLACK_AND_WHITE);
  params.setColorModeUserSet(true);
  params.setColorModePresetSet(true);
  BOOST_CHECK(params.isColorModeUserSet());
  BOOST_CHECK(params.isColorModePresetSet());

  QDomDocument document;
  const output::ColorParams restored(params.toXml(document, QStringLiteral("color-params")));
  BOOST_CHECK(restored.isColorModeUserSet());
  BOOST_CHECK(restored.isColorModePresetSet());

  output::ColorParams manuallyChanged = restored;
  manuallyChanged.setColorMode(output::COLOR);
  manuallyChanged.setColorModeUserSet(true);
  BOOST_CHECK(manuallyChanged.isColorModeUserSet());
  BOOST_CHECK(!manuallyChanged.isColorModePresetSet());
}

BOOST_AUTO_TEST_CASE(clear_detection_cache_makes_every_seeded_page_undecided) {
  finalize::Settings finalizeSettings;
  constexpr int pageCount = 4;

  for (int page = 1; page <= pageCount; ++page) {
    const PageId pageId(ImageId(QStringLiteral("/tmp/redetect-page.tif"), page));
    finalizeSettings.setColorMode(pageId, finalize::ColorMode::Color);
    finalizeSettings.setProcessed(pageId, true);
    BOOST_CHECK(!finalizeSettings.isColorModeDetectionNeeded(pageId));
  }

  finalizeSettings.clearDetectionCache();

  for (int page = 1; page <= pageCount; ++page) {
    const PageId pageId(ImageId(QStringLiteral("/tmp/redetect-page.tif"), page));
    BOOST_CHECK(finalizeSettings.isColorModeDetectionNeeded(pageId));
    BOOST_CHECK(!finalizeSettings.isProcessed(pageId));
  }
}

BOOST_AUTO_TEST_CASE(detector_version_and_sensitivity_invalidate_only_automatic_verdicts) {
  finalize::Settings settings;
  const PageId automatic(ImageId(QStringLiteral("/tmp/automatic.tif"), 0));
  const PageId manual(ImageId(QStringLiteral("/tmp/manual.tif"), 0));

  settings.setDetectedColorMode(automatic, finalize::ColorMode::BlackAndWhite);
  settings.setColorMode(manual, finalize::ColorMode::Color);
  BOOST_CHECK(!settings.isColorModeDetectionNeeded(automatic));
  BOOST_CHECK(!settings.isColorModeDetectionNeeded(manual));

  std::unique_ptr<finalize::Params> stale = settings.getParams(automatic);
  BOOST_REQUIRE(stale);
  stale->setDetectorSchemaVersion(LeptonicaDetector::DETECTOR_SCHEMA_VERSION - 1);
  settings.setParams(automatic, *stale);
  BOOST_CHECK(settings.isColorModeDetectionNeeded(automatic));
  BOOST_CHECK(!settings.isColorModeDetectionNeeded(manual));

  settings.setDetectedColorMode(automatic, finalize::ColorMode::BlackAndWhite);
  settings.setMidtoneThreshold(settings.midtoneThreshold() + 1);
  BOOST_CHECK(settings.isColorModeDetectionNeeded(automatic));
  BOOST_CHECK(!settings.isColorModeDetectionNeeded(manual));
}

BOOST_AUTO_TEST_CASE(automatic_detection_metadata_round_trips_through_xml) {
  finalize::Params params;
  params.setColorMode(finalize::ColorMode::Mixed);
  params.setColorModeDetected(true);
  params.setAutomaticDetection(true);
  params.setDetectorSchemaVersion(LeptonicaDetector::DETECTOR_SCHEMA_VERSION);
  params.setDetectionSensitivity(8);

  QDomDocument document;
  const finalize::Params restored(params.toXml(document, QStringLiteral("params")));
  BOOST_CHECK(restored.isAutomaticDetection());
  BOOST_CHECK(restored.detectorSchemaVersion()
              == LeptonicaDetector::DETECTOR_SCHEMA_VERSION);
  BOOST_CHECK(restored.detectionSensitivity() == 8);
}

BOOST_AUTO_TEST_SUITE_END()
