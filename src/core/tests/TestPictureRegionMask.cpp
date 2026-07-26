#include <BWColor.h>
#include <BinaryImage.h>
#include <ImageCombination.h>
#include <PhotoFrameDetector.h>

#include <QColor>
#include <QDomDocument>
#include <QImage>
#include <QTransform>
#include <boost/test/unit_test.hpp>

#include "filters/output/Params.h"
#include "filters/output/PictureRegionMask.h"

namespace output {
namespace tests {

BOOST_AUTO_TEST_SUITE(PictureRegionMaskTestSuite)

BOOST_AUTO_TEST_CASE(white_mask_pixels_preserve_continuous_tone) {
  QImage continuousTone(2, 1, QImage::Format_RGB32);
  continuousTone.fill(QColor(160, 160, 160));

  imageproc::BinaryImage binarized(2, 1, imageproc::BLACK);
  imageproc::BinaryImage pictureMask(2, 1, imageproc::BLACK);
  pictureMask.fill(QRect(1, 0, 1, 1), imageproc::WHITE);

  imageproc::combineImages(continuousTone, binarized, pictureMask);

  BOOST_CHECK_EQUAL(qGray(continuousTone.pixel(0, 0)), 0);
  BOOST_CHECK_EQUAL(qGray(continuousTone.pixel(1, 0)), 160);
}

BOOST_AUTO_TEST_CASE(region_evidence_defaults_empty_and_round_trips_xml) {
  Params defaults;
  BOOST_CHECK(defaults.continuousToneRegions().isEmpty());
  BOOST_CHECK(defaults.pictureFrames().isEmpty());

  ContinuousToneRegions regions;
  regions.setAnalysisSize(QSize(1200, 800));
  regions.setSourceRect(QRect(101, 203, 2400, 1600));
  regions.setBounds({QRect(300, 200, 500, 400)});
  defaults.setContinuousToneRegions(regions);
  PictureFrames frames;
  frames.setAnalysisSize(QSize(1200, 800));
  frames.setSourceRect(QRect(101, 203, 2400, 1600));
  frames.setFrames({PictureFrame{QRect(312, 211, 476, 378),
                                 QStringLiteral("vision+cv;continuous_tone")}});
  defaults.setPictureFrames(frames);

  QDomDocument document;
  const Params restored(defaults.toXml(document, QStringLiteral("params")));
  BOOST_CHECK(restored.continuousToneRegions() == regions);
  BOOST_CHECK(restored.pictureFrames() == frames);

  const Params oldProject(document.createElement(QStringLiteral("params")));
  BOOST_CHECK(oldProject.continuousToneRegions().isEmpty());
  BOOST_CHECK(oldProject.pictureFrames().isEmpty());
}

BOOST_AUTO_TEST_CASE(region_mapping_uses_analysis_scale_and_detection_crop_origin) {
  ContinuousToneRegions regions;
  regions.setAnalysisSize(QSize(100, 50));
  regions.setSourceRect(QRect(10, 20, 200, 100));
  regions.setBounds({QRect(25, 10, 50, 20)});

  imageproc::BinaryImage mask(400, 250, imageproc::BLACK);
  const QRect expectedMappedBounds(100, 90, 200, 120);
  mask.fill(QRect(100, 90, 80, 120), imageproc::WHITE);  // exactly 40%
  mask.fill(QRect(5, 5, 3, 3), imageproc::WHITE);        // outside sentinel
  imageproc::BinaryImage expected(mask);
  expected.fill(expectedMappedBounds, imageproc::WHITE);

  const PictureRegionFillResult result = fillPictureRegionHoles(
      mask, regions, QTransform::fromScale(2.0, 3.0), QPoint(20, 30));

  BOOST_REQUIRE_EQUAL(result.mappedRegions.size(), 1);
  BOOST_CHECK(result.mappedRegions.front() == expectedMappedBounds);
  BOOST_CHECK_EQUAL(result.filledRegionCount, 1);
  BOOST_CHECK(mask == expected);  // proves the diff outside the mapped rect is zero
}

BOOST_AUTO_TEST_CASE(region_mapping_handles_quarter_turn_without_off_by_scale_growth) {
  ContinuousToneRegions regions;
  regions.setAnalysisSize(QSize(100, 50));
  regions.setSourceRect(QRect(0, 0, 100, 50));
  regions.setBounds({QRect(10, 5, 20, 10)});

  imageproc::BinaryImage mask(50, 100, imageproc::BLACK);
  mask.fill(QRect(35, 10, 4, 20), imageproc::WHITE);  // 40% of rotated rect
  const QTransform quarterTurn(0.0, 1.0, -1.0, 0.0, 50.0, 0.0);
  const PictureRegionFillResult result = fillPictureRegionHoles(
      mask, regions, quarterTurn, QPoint());

  BOOST_REQUIRE_EQUAL(result.mappedRegions.size(), 1);
  BOOST_CHECK(result.mappedRegions.front() == QRect(35, 10, 10, 20));
  BOOST_CHECK_EQUAL(result.filledRegionCount, 1);
  BOOST_CHECK_EQUAL(mask.countWhitePixels(QRect(35, 10, 10, 20)), 200);
}

BOOST_AUTO_TEST_CASE(region_below_picture_coverage_is_unchanged) {
  ContinuousToneRegions regions;
  regions.setAnalysisSize(QSize(100, 100));
  regions.setSourceRect(QRect(0, 0, 100, 100));
  regions.setBounds({QRect(20, 20, 50, 50)});

  imageproc::BinaryImage mask(100, 100, imageproc::BLACK);
  mask.fill(QRect(20, 20, 19, 50), imageproc::WHITE);  // 38%
  const imageproc::BinaryImage before(mask);

  const PictureRegionFillResult result = fillPictureRegionHoles(
      mask, regions, QTransform(), QPoint());

  BOOST_CHECK_EQUAL(result.filledRegionCount, 0);
  BOOST_CHECK(mask == before);
}

BOOST_AUTO_TEST_CASE(accepted_frame_fills_exact_bounds_without_coverage_gate) {
  PictureFrames frames;
  frames.setAnalysisSize(QSize(100, 100));
  frames.setSourceRect(QRect(10, 20, 200, 200));
  frames.setFrames({PictureFrame{QRect(20, 25, 40, 30), QStringLiteral("cv")}});

  imageproc::BinaryImage mask(250, 250, imageproc::BLACK);
  mask.fill(QRect(3, 4, 2, 2), imageproc::WHITE);
  imageproc::BinaryImage expected(mask);
  const QRect exactMapped(50, 70, 80, 60);
  expected.fill(exactMapped, imageproc::WHITE);

  const PictureRegionFillResult result =
      fillPictureFrames(mask, frames, QTransform(), QPoint());
  BOOST_REQUIRE_EQUAL(result.mappedRegions.size(), 1);
  BOOST_CHECK(result.mappedRegions.front() == exactMapped);
  BOOST_CHECK_EQUAL(result.filledRegionCount, 1);
  BOOST_CHECK(mask == expected);
}

namespace {

LeptonicaDetector::DetectionEvidence syntheticEvidence(
    const QRect& toneRegion, const bool continuousTone, const bool lineArt) {
  LeptonicaDetector::DetectionEvidence evidence;
  evidence.analysisSize = QSize(900, 900);
  evidence.regionBounds = {toneRegion};
  evidence.largestRegionBounds = toneRegion;
  int index = 0;
  for (int gridY = 0; gridY < 9; ++gridY) {
    for (int gridX = 0; gridX < 9; ++gridX) {
      auto& tile = evidence.toneTiles[index++];
      tile.gridX = gridX;
      tile.gridY = gridY;
      tile.x = gridX * 100;
      tile.y = gridY * 100;
      tile.width = 180;
      tile.height = 180;
      const QRect tileRect(tile.x, tile.y, tile.width, tile.height);
      if (tileRect.intersects(toneRegion)) {
        tile.continuousTone = continuousTone;
        tile.lineArtRejected = lineArt;
      }
    }
  }
  return evidence;
}

}  // namespace

BOOST_AUTO_TEST_CASE(frame_acceptance_keeps_framed_continuous_tone_photo) {
  const QRect photo(310, 220, 360, 430);
  const auto evidence = syntheticEvidence(photo, true, false);
  PhotoFrameDetector::Candidate candidate{
      photo, 0.42, 0.9, QStringLiteral("cv_edge_runs_rectangle")};
  const auto accepted =
      PhotoFrameDetector::acceptCandidates(evidence.analysisSize, evidence, {candidate});
  BOOST_REQUIRE_EQUAL(accepted.size(), 1);
  BOOST_CHECK(accepted.front().bounds == photo);
  BOOST_CHECK(accepted.front().reason.contains(QStringLiteral("continuous_tone_tiles")));
}

BOOST_AUTO_TEST_CASE(frame_acceptance_rejects_text_block_line_art) {
  const QRect textBlock(180, 180, 520, 500);
  const auto evidence = syntheticEvidence(textBlock, false, true);
  PhotoFrameDetector::Candidate candidate{
      textBlock, 0.55, 0.95, QStringLiteral("vision+gradient_snap")};
  const auto accepted =
      PhotoFrameDetector::acceptCandidates(evidence.analysisSize, evidence, {candidate});
  BOOST_CHECK(accepted.isEmpty());
}

BOOST_AUTO_TEST_CASE(frame_acceptance_rejects_full_page_plate) {
  const QRect plate(20, 20, 860, 860);
  const auto evidence = syntheticEvidence(plate, true, false);
  PhotoFrameDetector::Candidate candidate{
      plate, 0.60, 0.99, QStringLiteral("vision+cv_gradient_snap")};
  const auto accepted =
      PhotoFrameDetector::acceptCandidates(evidence.analysisSize, evidence, {candidate});
  BOOST_CHECK(accepted.isEmpty());
}

BOOST_AUTO_TEST_CASE(frame_acceptance_keeps_outermost_overlapping_frame) {
  const QRect outer(180, 180, 520, 500);
  const QRect inner(260, 250, 300, 280);
  const auto evidence = syntheticEvidence(outer, true, false);
  const QVector<PhotoFrameDetector::Candidate> candidates{
      {inner, 0.75, 0.9, QStringLiteral("cv_edge_runs_rectangle")},
      {outer, 0.70, 0.9, QStringLiteral("vision+cv_gradient_snap")}};
  const auto accepted =
      PhotoFrameDetector::acceptCandidates(evidence.analysisSize, evidence, candidates);
  BOOST_REQUIRE_EQUAL(accepted.size(), 1);
  BOOST_CHECK(accepted.front().bounds == outer);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace tests
}  // namespace output
