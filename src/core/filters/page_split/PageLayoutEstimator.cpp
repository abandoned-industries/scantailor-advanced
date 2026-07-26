// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PageLayoutEstimator.h"

#include <Binarize.h>
#include <BinaryThreshold.h>
#include <ConnComp.h>
#include <ConnCompEraserExt.h>
#include <Connectivity.h>
#include <Constants.h>
#include <GrayRasterOp.h>
#include <Grayscale.h>
#include <Morphology.h>
#include <PolygonRasterizer.h>
#include <RasterOp.h>
#include <ReduceThreshold.h>
#include <Scale.h>
#include <SeedFill.h>
#include <Shear.h>
#include <SkewFinder.h>
#include <SlicedHistogram.h>
#include <Transform.h>
#include <imageproc/OrthogonalRotation.h>

#include <QDebug>
#include <QPainter>
#include <boost/foreach.hpp>
#include <limits>
#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

#include "AppleVisionDetector.h"
#include "ContentSpanFinder.h"
#include "DebugImages.h"
#include "ImageMetadata.h"
#include "ImageTransformation.h"
#include "OrthogonalRotation.h"
#include "PageLayout.h"
#include "PlatePrior.h"
#include "ProjectPages.h"
#include "SpineDarknessFinder.h"
#include "VertLineFinder.h"

namespace page_split {
using namespace imageproc;

namespace {
double lineCenterX(const QLineF& line) {
  return 0.5 * (line.p1().x() + line.p2().x());
}

struct CenterComparator {
  bool operator()(const QLineF& line1, const QLineF& line2) const { return lineCenterX(line1) < lineCenterX(line2); }
};

/**
 * \brief Try to auto-detect a page layout for a single-page configuration.
 *
 * \param layoutType The requested layout type.  The layout type of
 *        SINGLE_PAGE_UNCUT is not handled here.
 * \param ltrLines Folding line candidates sorted from left to right.
 * \param imageSize The dimentions of the page image.
 * \param hor_shadows A downscaled grayscale image that contains
 *        long enough and not too thin horizontal lines.
 * \param dbg An optional sink for debugging images.
 * \return The page layout detected or a null unique_ptr.
 */
std::unique_ptr<PageLayout> autoDetectSinglePageLayout(const LayoutType layoutType,
                                                       const std::vector<QLineF>& ltrLines,
                                                       const QRectF& virtualImageRect,
                                                       const GrayImage& grayDownscaled,
                                                       const QTransform& outToDownscaled,
                                                       DebugImages* dbg) {
  const double imageCenter = virtualImageRect.center().x();

  // A loop just to be able to break from it.
  do {
    // This whole branch (loop) leads to SINGLE_PAGE_UNCUT,
    // which conflicts with PAGE_PLUS_OFFCUT.
    if (layoutType == PAGE_PLUS_OFFCUT) {
      break;
    }
    // If we have a single line close to an edge,
    // Or more than one line, with the first and the last
    // ones close to an edge, that looks more like
    // SINGLE_PAGE_CUT layout.
    if (!ltrLines.empty()) {
      const QLineF& firstLine = ltrLines.front();
      const double lineCenter = lineCenterX(firstLine);
      if (std::fabs(imageCenter - lineCenter) > 0.65 * imageCenter) {
        break;
      }
    }
    if (ltrLines.size() > 1) {
      const QLineF& lastLine = ltrLines.back();
      const double lineCenter = lineCenterX(lastLine);
      if (std::fabs(imageCenter - lineCenter) > 0.65 * imageCenter) {
        break;
      }
    }

    // Return a SINGLE_PAGE_UNCUT layout.
    return std::make_unique<PageLayout>(virtualImageRect);
  } while (false);

  if (ltrLines.empty()) {
    // Impossible to detect the layout type.
    return nullptr;
  } else if (ltrLines.size() > 1) {
    return std::make_unique<PageLayout>(virtualImageRect, ltrLines.front(), ltrLines.back());
  } else {
    assert(ltrLines.size() == 1);
    const QLineF& line = ltrLines.front();
    const double lineCenter = lineCenterX(line);
    if (lineCenter < imageCenter) {
      const QLineF rightLine(virtualImageRect.topRight(), virtualImageRect.bottomRight());
      return std::make_unique<PageLayout>(virtualImageRect, line, rightLine);
    } else {
      const QLineF leftLine(virtualImageRect.topLeft(), virtualImageRect.bottomLeft());
      return std::make_unique<PageLayout>(virtualImageRect, leftLine, line);
    }
  }
}  // autoDetectSinglePageLayout

/**
 * \brief Try to auto-detect a page layout for a two-page configuration.
 *
 * \param ltrLines Folding line candidates sorted from left to right.
 * \param imageSize The dimentions of the page image.
 * \return The page layout detected or a null unique_ptr.
 */
std::unique_ptr<PageLayout> autoDetectTwoPageLayout(const std::vector<QLineF>& ltrLines,
                                                    const QRectF& virtualImageRect) {
  if (ltrLines.empty()) {
    // Impossible to detect the page layout.
    return nullptr;
  } else if (ltrLines.size() == 1) {
    return std::make_unique<PageLayout>(virtualImageRect, ltrLines.front());
  }

  // Find the line closest to the center.
  const double imageCenter = virtualImageRect.center().x();
  double minDistance = std::numeric_limits<double>::max();
  const QLineF* bestLine = nullptr;
  for (const QLineF& line : ltrLines) {
    const double lineCenter = lineCenterX(line);
    const double distance = std::fabs(lineCenter - imageCenter);
    if (distance < minDistance) {
      minDistance = distance;
      bestLine = &line;
    }
  }
  return std::make_unique<PageLayout>(virtualImageRect, *bestLine);
}

int numPages(const LayoutType layoutType, const ImageTransformation& preXform) {
  int numPages = 0;

  switch (layoutType) {
    case AUTO_LAYOUT_TYPE: {
      const QSize imageSize(preXform.origRect().size().toSize());
      numPages = ProjectPages::adviseNumberOfLogicalPages(ImageMetadata(imageSize, preXform.origDpi()),
                                                          preXform.preRotation());
      break;
    }
    case SINGLE_PAGE_UNCUT:
    case PAGE_PLUS_OFFCUT:
      numPages = 1;
      break;
    case TWO_PAGES:
      numPages = 2;
      break;
  }
  return numPages;
}
}  // anonymous namespace

PageLayout PageLayoutEstimator::estimatePageLayout(const LayoutType layoutType,
                                                   const QImage& input,
                                                   const ImageTransformation& preXform,
                                                   const BinaryThreshold bwThreshold,
                                                   DebugImages* const dbg,
                                                   QString* const decisionReason) {
  if (layoutType == SINGLE_PAGE_UNCUT) {
    return PageLayout(preXform.resultingRect());
  }

  std::unique_ptr<PageLayout> layout(
      tryCutAtFoldingLine(layoutType, input, preXform, dbg, decisionReason));
  if (layout) {
    if (decisionReason && layout->type() == PageLayout::SINGLE_PAGE_UNCUT
        && *decisionReason
            != QStringLiteral("vision_split_vetoed_near_square_no_gutter")
        && PlatePrior::analyze(input).isPlate) {
      *decisionReason =
          QStringLiteral("plate_prior_continuous_tone_caption_scale_text_geometry_single");
    }
    return *layout;
  }
  return cutAtWhitespace(layoutType, input, preXform, bwThreshold, dbg);
}

namespace {
class BadTwoPageSplitter {
 public:
  explicit BadTwoPageSplitter(double imageWidth)
      : m_imageCenter(0.5 * imageWidth), m_distFromCenterThreshold(0.6 * m_imageCenter) {}

  /**
   * Returns true if the line is too close to an edge
   * to be the line splitting two pages
   */
  bool operator()(const QLineF& line) {
    const double dist = std::fabs(lineCenterX(line) - m_imageCenter);
    return dist > m_distFromCenterThreshold;
  }

 private:
  double m_imageCenter;
  double m_distFromCenterThreshold;
};
}  // anonymous namespace

/**
 * \brief Attempts to find the folding line and cut the image there.
 *
 * \param layoutType The type of a layout to detect.  If set to
 *        something other than AUTO_LAYOUT_TYPE, the returned
 *        layout will have the same type.  The layout type of
 *        SINGLE_PAGE_UNCUT is not handled here.
 * \param input The input image.  Will be converted to grayscale unless
 *        it's already grayscale.
 * \param preXform The logical transformation applied to the input image.
 *        The resulting page layout will be in transformed coordinates.
 * \param dbg An optional sink for debugging images.
 * \return The detected page layout, or a null unique_ptr if page layout
 *         could not be detected.
 */
std::unique_ptr<PageLayout> PageLayoutEstimator::tryCutAtFoldingLine(const LayoutType layoutType,
                                                                     const QImage& input,
                                                                     const ImageTransformation& preXform,
                                                                     DebugImages* const dbg,
                                                                     QString* const decisionReason) {
  const int numPages = page_split::numPages(layoutType, preXform);
  const QRectF virtualImageRect(preXform.transform().mapRect(input.rect()));

  // A split candidate (in normalized [0,1] virtual width) is only trusted
  // if it lies within this central window. Anything outside is treated as
  // bogus and we fall through to the next algorithm. This catches cases
  // where Vision returns a confident-but-wrong split jammed against an edge.
  constexpr double kCentralLo = 0.20;
  constexpr double kCentralHi = 0.80;
  auto splitFractionIsCentral = [](double f) { return f >= kCentralLo && f <= kCentralHi; };

  // Text-boundary anchor: when Vision detects text only on one side of
  // the spread (e.g. left=28, right=0 on a text+photo spread), the
  // rightmost text region boundary approximates where the gutter is.
  // Captured here so it survives beyond the Vision block's scope.
  double textBoundaryAnchorX = std::numeric_limits<double>::quiet_NaN();

  // Try Apple Vision-based page split detection first (macOS only)
  if (AppleVisionDetector::isAvailable() && (layoutType == AUTO_LAYOUT_TYPE || layoutType == TWO_PAGES)) {
    constexpr int kVisionMaxDimension = 1800;
    QImage visionInput = input;
    if (input.width() > kVisionMaxDimension || input.height() > kVisionMaxDimension) {
      visionInput = input.scaled(
          kVisionMaxDimension, kVisionMaxDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    const auto visionResult = AppleVisionDetector::detectPageSplit(visionInput);
    const PlatePrior::Evidence platePrior = PlatePrior::analyze(visionInput);
    QVector<QRectF> textBounds;
    textBounds.reserve(visionResult.textRegions.size());
    for (const auto& region : visionResult.textRegions) textBounds.append(region.bounds);
    const PlatePrior::CaptionEvidence captionEvidence =
        PlatePrior::analyzeCaptionText(textBounds, visionInput.size());
    const PlatePrior::SpreadEvidence spreadEvidence =
        PlatePrior::analyzeSpreadGeometry(input);
    const auto recordNoGutterReason = [&]() {
      if (decisionReason) {
        *decisionReason =
            spreadEvidence.aspectRatio < 1.25
                ? QStringLiteral("near_square_no_gutter_evidence")
                : QStringLiteral("moderate_aspect_no_uniform_gutter_evidence");
      }
    };
    if (layoutType == AUTO_LAYOUT_TYPE && platePrior.isPlate
        && captionEvidence.isCaptionScale && !spreadEvidence.isSpread) {
      recordNoGutterReason();
      qDebug() << SpineDarknessFinder::logPageTag()
               << "PageLayoutEstimator:" << platePrior.reason << captionEvidence.reason
               << spreadEvidence.reason
               << "forcing SINGLE_PAGE_UNCUT";
      return std::make_unique<PageLayout>(virtualImageRect);
    }
    if (visionResult.shouldSplit && visionResult.confidence >= 0.60f
        && splitFractionIsCentral(visionResult.splitLineX)) {
      // On AUTO, text columns alone are not physical evidence of two leaves.
      // Accept affirmative Vision only when the page is at least moderately
      // wide or the round-2 full-height uniform-gutter test independently
      // finds a real separation. Forced TWO_PAGES remains unchanged.
      const bool acceptAffirmativeVision =
          layoutType == TWO_PAGES
          || spreadEvidence.aspectRatio >= 1.25
          || spreadEvidence.hasFullHeightGutter;
      if (!acceptAffirmativeVision) {
        if (decisionReason) {
          *decisionReason =
              QStringLiteral("vision_split_vetoed_near_square_no_gutter");
        }
        qDebug() << SpineDarknessFinder::logPageTag()
                 << "PageLayoutEstimator: affirmative Vision split vetoed:"
                 << spreadEvidence.reason << "aspect"
                 << spreadEvidence.aspectRatio;
        return std::make_unique<PageLayout>(virtualImageRect);
      }

      // Convert Vision's normalized split position to image coordinates.
      const double visionSplitX = visionResult.splitLineX * virtualImageRect.width() + virtualImageRect.left();

      // Refinement pass: Vision (and VertLineFinder) often latch onto a
      // strong vertical edge inside an image on the spread (e.g. an
      // illustration's edge or a column in a carved frieze) rather than
      // the actual gutter shadow, which can be 10-15% of width away.
      // Run SpineDarknessFinder with a wide ±15% window centered on
      // Vision's position. Its dark-row-fraction and prominence gates
      // discriminate gutter shadows (full-height dark column) from
      // image edges (partial-height dark column), so a wide window is
      // safe — if no actual gutter exists nearby, the gates reject the
      // candidate and we keep Vision's exact result.
      GrayImage refineGray;
      QTransform refineXform;
      VertLineFinder::buildGrayDownscaled(input, preXform, &refineGray, &refineXform);
      bool broadGutterRescue = false;
      const bool strongTwoPageNumberAnchor = visionResult.hasPageNumbers && visionResult.confidence >= 0.80f;
      const bool singlePageNumberAnchor =
          !visionResult.hasPageNumbers && visionResult.hasAnyPageNumber && visionResult.confidence >= 0.70f;
      const bool preferAnchorIfBroadGutter = strongTwoPageNumberAnchor || singlePageNumberAnchor;
      const QLineF refinedSpine = SpineDarknessFinder::findSpine(
          refineGray, refineXform, virtualImageRect,
          /*centerWindowFraction=*/0.15,
          /*maxTiltDegrees=*/2.0,
          /*centerXOverride=*/visionSplitX, dbg, &broadGutterRescue,
          preferAnchorIfBroadGutter,
          /*keepExactAnchorIfBroadGutter=*/singlePageNumberAnchor);

      if (!refinedSpine.isNull()) {
        // Refinement leash: the refined position must stay within this
        // fraction of page width from Vision's anchor. Originally this
        // was tighter (5%) as a safety net for when the SpineDarknessFinder
        // gates were buggy and let photo-edge candidates through. With the
        // gates now properly two-sided (paper-side + peak-prominence on
        // both neighbors), refinements that pass them are reliable real
        // spine shadows, and we can trust the search across a wider
        // radius. 10% covers Vision being slightly off-center on
        // asymmetric spreads (text page + full-page photo) without
        // accepting absurd jumps that almost certainly indicate Vision
        // and the refinement disagree about which column is the spine.
        const double refinedX = lineCenterX(refinedSpine);
        constexpr double kRefineLeashFraction = 0.10;
        constexpr double kBroadGutterLeashFraction = 0.09;
        const double maxLeashDelta = kRefineLeashFraction * virtualImageRect.width();
        const double maxBroadGutterDelta = kBroadGutterLeashFraction * virtualImageRect.width();
        if (std::fabs(refinedX - visionSplitX) <= maxLeashDelta
            || (broadGutterRescue && std::fabs(refinedX - visionSplitX) <= maxBroadGutterDelta)) {
          qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: [SPINE-REFINE] Vision split at" << visionSplitX
                   << "(conf" << visionResult.confidence << ") refined to" << refinedSpine
                   << (broadGutterRescue ? "[broad gutter]" : "");
          return std::make_unique<PageLayout>(virtualImageRect, refinedSpine);
        }
        qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: [SPINE-REFINE] discarding refinement at" << refinedX
                 << "— too far from Vision anchor" << visionSplitX
                 << "(delta=" << std::fabs(refinedX - visionSplitX)
                 << "> leash=" << maxLeashDelta << ")";
        // Fall through to the Vision-only path below.
      }

      qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: Using Vision-detected split at" << visionResult.splitLineX
               << "confidence:" << visionResult.confidence << "(no dark refinement found)";
      const QLineF splitLine(visionSplitX, virtualImageRect.top(), visionSplitX, virtualImageRect.bottom());
      return std::make_unique<PageLayout>(virtualImageRect, splitLine);
    } else if (layoutType == TWO_PAGES) {
      // User explicitly forced TWO_PAGES layout - honor their request.
      // Use Vision's position only if it is centrally plausible; otherwise
      // fall through so VertLineFinder / SpineDarknessFinder / center
      // fallback can take a swing.
      if (splitFractionIsCentral(visionResult.splitLineX)) {
        const double splitX = visionResult.splitLineX * virtualImageRect.width() + virtualImageRect.left();
        qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: Forced TWO_PAGES, using Vision split at" << splitX
                 << "(confidence:" << visionResult.confidence << ")";
        const QLineF splitLine(splitX, virtualImageRect.top(), splitX, virtualImageRect.bottom());
        return std::make_unique<PageLayout>(virtualImageRect, splitLine);
      }
      qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: Forced TWO_PAGES, Vision splitX" << visionResult.splitLineX
               << "is not central - falling through to traditional detection";
      // fall through
    } else if (visionResult.leftTextRegions > 0 || visionResult.rightTextRegions > 0) {
      // Vision saw text but didn't find a clear two-page split.
      // The original code treated this as conclusive and returned single
      // page, on the theory that the image must be a single-page
      // two-column layout. That's true sometimes — but it also misfires
      // on every asymmetric spread (chapter title pages, title pages,
      // image-heavy spreads with a tiny caption on one side, etc.) and
      // costs us a lot of false negatives.
      //
      // Trust geometry as the tiebreaker. If the geometric advise()
      // function already concluded numPages == 2 (image is wider than
      // tall in physical units), the image is almost certainly a real
      // two-page spread regardless of how the text regions are
      // distributed, and we should fall through to traditional detection
      // + SpineDarknessFinder + center fallback. Only short-circuit to
      // single page when geometry ALSO says it's a single page
      // (numPages == 1), where Vision's vote actually agrees with the
      // shape of the image.
      const int leftCount = visionResult.leftTextRegions;
      const int rightCount = visionResult.rightTextRegions;
      if (numPages == 2) {
        qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: [SPINE-FALLBACK] Vision uncertain"
                 << "(left:" << leftCount << "right:" << rightCount << ")"
                 << "but geometry says spread (numPages=2) - falling through to traditional detection";
        // When text is heavily one-sided, use the text boundary as the
        // search anchor for SpineDarknessFinder. This helps find faint
        // gutter shadows on spreads where one page is a full-bleed photo
        // and the other is text (e.g. left=28, right=0).
        if (leftCount >= 3 && rightCount == 0 && visionResult.rightmostLeftTextX > 0) {
          textBoundaryAnchorX = visionResult.rightmostLeftTextX * virtualImageRect.width() + virtualImageRect.left();
          qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: text-boundary anchor at"
                   << textBoundaryAnchorX << "(rightmostLeftTextX=" << visionResult.rightmostLeftTextX << ")";
        }
        // fall through
      } else {
        recordNoGutterReason();
        qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: [SPINE-FALLBACK] Vision uncertain"
                 << "(left:" << leftCount << "right:" << rightCount << ")"
                 << "and geometry says single (numPages=" << numPages << ") - returning single page layout";
        return std::make_unique<PageLayout>(virtualImageRect);  // Single page, no split
      }
    } else {
      // No text regions detected (photo page, blank page, etc.)
      // Check aspect ratio: if image is clearly portrait, assume single page.
      // For square-ish or landscape images without text, fall through to traditional.
      const double aspectRatio = static_cast<double>(input.width()) / input.height();
      if (aspectRatio < 0.85) {
        // Clearly portrait - definitely a single page
        recordNoGutterReason();
        qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: Vision found no text, aspect ratio" << aspectRatio
                 << "is portrait - returning single page layout";
        return std::make_unique<PageLayout>(virtualImageRect);  // Single page, no split
      }
      // For square-ish or landscape images without text, fall through to traditional algorithm
      qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: Vision found no text, aspect ratio" << aspectRatio
               << "- using traditional detection";
    }
  }

  // AUTO layout may only reach the legacy line and whitespace splitters when
  // the page itself has plausible spread geometry. A successful Vision text
  // split has already returned above, while explicit TWO_PAGES continues
  // untouched. This prevents square single leaves from inheriting the old
  // "wider than tall means two pages" fallback.
  if (layoutType == AUTO_LAYOUT_TYPE) {
    const PlatePrior::SpreadEvidence spreadEvidence =
        PlatePrior::analyzeSpreadGeometry(input);
    if (!spreadEvidence.isSpread) {
      if (decisionReason) {
        *decisionReason =
            spreadEvidence.aspectRatio < 1.25
                ? QStringLiteral("near_square_no_gutter_evidence")
                : QStringLiteral("moderate_aspect_no_uniform_gutter_evidence");
      }
      qDebug() << SpineDarknessFinder::logPageTag()
               << "PageLayoutEstimator:" << spreadEvidence.reason
               << "rejecting non-Vision AUTO split";
      return std::make_unique<PageLayout>(virtualImageRect);
    }
  }

  GrayImage grayDownscaled;
  QTransform outToDownscaled;

  // We always capture grayDownscaled / outToDownscaled now, because the
  // SpineDarknessFinder fallback (used in the two-page branch below) needs
  // them in addition to autoDetectSinglePageLayout.
  const int maxLines = 8;
  std::vector<QLineF> lines(
      VertLineFinder::findLines(input, preXform, maxLines, dbg, &grayDownscaled, &outToDownscaled));

  std::sort(lines.begin(), lines.end(), CenterComparator());

  const QPointF center(virtualImageRect.center());

  if (numPages == 1) {
    // If all of the lines are close to one of the edges,
    // that means they can't be the edges of a pages,
    // so we take only one of them, the one closest to
    // the center.
    while (lines.size() > 1) {  // just to be able to break from it.
      const QLineF leftLine(lines.front());
      const QLineF rightLine(lines.back());
      const double threshold = 0.3 * center.x();
      double leftDist = center.x() - lineCenterX(leftLine);
      double rightDist = center.x() - lineCenterX(rightLine);
      if ((leftDist < 0) != (rightDist < 0)) {
        // They are from the opposite sides
        // from the center line.
        break;
      }

      leftDist = std::fabs(leftDist);
      rightDist = std::fabs(rightDist);
      if ((leftDist < threshold) || (rightDist < threshold)) {
        // At least one of them is relatively close
        // to the center.
        break;
      }

      lines.clear();
      lines.push_back(leftDist < rightDist ? leftLine : rightLine);
      break;
    }
    return autoDetectSinglePageLayout(layoutType, lines, virtualImageRect, grayDownscaled, outToDownscaled, dbg);
  } else {
    assert(numPages == 2);
    // In two page mode we ignore the lines that are too close
    // to the edge.
    lines.erase(std::remove_if(lines.begin(), lines.end(), BadTwoPageSplitter(virtualImageRect.width())), lines.end());

    // Fallback chain when VertLineFinder produced no usable candidates:
    //   1. SpineDarknessFinder — looks for a near-vertical strip of
    //      consistently dark pixels (gutter shadow) within ±10% of center.
    //      Helps when the gradient/Hough detector misses a soft shadow.
    //   2. Geometric center split — if nothing found a candidate but the
    //      input geometry is unmistakably a two-page spread, just split
    //      down the middle. Far better than letting cutAtWhitespace
    //      collapse an obvious spread to a single page.
    if (lines.empty() && !grayDownscaled.isNull()) {
      const QLineF spine = SpineDarknessFinder::findSpine(
          grayDownscaled, outToDownscaled, virtualImageRect,
          /*centerWindowFraction=*/0.10,
          /*maxTiltDegrees=*/2.0,
          /*centerXOverride=*/textBoundaryAnchorX, dbg);
      if (!spine.isNull()) {
        qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: using SpineDarknessFinder fallback at" << spine;
        return std::make_unique<PageLayout>(virtualImageRect, spine);
      }
    }

    if (lines.empty()) {
      // Geometry-based last resort: numPages == 2 means the advise()
      // function already concluded the image is wide enough to be a
      // spread. Splitting at the geometric center is the safest default
      // when every detector has failed.
      const double splitX = virtualImageRect.center().x();
      const QLineF centerLine(splitX, virtualImageRect.top(), splitX, virtualImageRect.bottom());
      qDebug() << SpineDarknessFinder::logPageTag() << "PageLayoutEstimator: [SPINE-FALLBACK] no detector produced a candidate, using"
               << "geometric center split at" << splitX << "(virtualImageRect=" << virtualImageRect << ")";
      return std::make_unique<PageLayout>(virtualImageRect, centerLine);
    }

    return autoDetectTwoPageLayout(lines, virtualImageRect);
  }
}  // PageLayoutEstimator::tryCutAtFoldingLine

/**
 * \brief Attempts to find a suitable whitespace to draw a splitting line through.
 *
 * \param layoutType The type of a layout to detect.  If set to
 *        something other than AUTO_LAYOUT_TYPE, the returned
 *        layout will have the same type.
 * \param input The input image.  Will be converted to grayscale unless
 *        it's already grayscale.
 * \param preXform The logical transformation applied to the input image.
 *        The resulting page layout will be in transformed coordinates.
 * \param bwThreshold The global binarization threshold for the input image.
 * \param dbg An optional sink for debugging images.
 * \return Even if no suitable whitespace was found, this function
 *         will return a PageLayout consistent with the layoutType requested.
 */
PageLayout PageLayoutEstimator::cutAtWhitespace(const LayoutType layoutType,
                                                const QImage& input,
                                                const ImageTransformation& preXform,
                                                const BinaryThreshold bwThreshold,
                                                DebugImages* const dbg) {
  QTransform xform;

  // Convert to B/W and rotate.
  BinaryImage img(to300DpiBinary(input, xform, bwThreshold));
  // Note: here we assume the only transformation applied
  // to the input image is orthogonal rotation.
  img = orthogonalRotation(img, preXform.preRotation().toDegrees());
  if (dbg) {
    dbg->add(img, "bw300");
  }

  img = removeGarbageAnd2xDownscale(img, dbg);
  xform.scale(0.5, 0.5);
  if (dbg) {
    dbg->add(img, "no_garbage");
  }

  // From now on we work with 150 dpi images.

  const bool leftOffcut = checkForLeftOffcut(img);
  const bool rightOffcut = checkForRightOffcut(img);

  SkewFinder skewFinder;
  // We work with 150dpi image, so no further reduction.
  skewFinder.setCoarseReduction(0);
  skewFinder.setFineReduction(0);
  skewFinder.setDesiredAccuracy(0.5);  // fine accuracy is not required.
  const Skew skew(skewFinder.findSkew(img));
  if ((skew.angle() != 0.0) && (skew.confidence() >= Skew::GOOD_CONFIDENCE)) {
    const int w = img.width();
    const int h = img.height();
    const double angleDeg = skew.angle();
    const double tg = std::tan(angleDeg * constants::DEG2RAD);

    const auto margin = (int) std::ceil(std::fabs(0.5 * h * tg));
    const int newWidth = w - margin * 2;
    if (newWidth > 0) {
      hShearInPlace(img, tg, 0.5 * h, WHITE);
      BinaryImage newImg(newWidth, h);
      rasterOp<RopSrc>(newImg, newImg.rect(), img, QPoint(margin, 0));
      img.swap(newImg);
      if (dbg) {
        dbg->add(img, "shear_applied");
      }

      QTransform t1;
      t1.translate(-0.5 * w, -0.5 * h);
      QTransform t2;
      t2.shear(tg, 0.0);
      QTransform t3;
      t3.translate(0.5 * w - margin, 0.5 * h);
      xform = xform * t1 * t2 * t3;
    }
  }

  const int numPages = page_split::numPages(layoutType, preXform);
  const PageLayout layout(cutAtWhitespaceDeskewed150(layoutType, numPages, img, leftOffcut, rightOffcut, dbg));

  PageLayout transformedLayout(layout.transformed(xform.inverted()));
  // We don't want a skewed outline!
  transformedLayout.setUncutOutline(preXform.resultingRect());
  return transformedLayout;
}  // PageLayoutEstimator::cutAtWhitespace

/**
 * \brief Attempts to find a suitable whitespace to draw a splitting line through.
 *
 * \param layoutType The type of a layout to detect.  If set to
 *        something other than AUTO_LAYOUT_TYPE, the returned
 *        layout will have the same type.
 * \param numPages The number of pages (1 or 2) in the layout.
 * \param input The black and white, 150 DPI input image.
 * \param leftOffcut True if there seems to be garbage on the left side.
 * \param rightOffcut True if there seems to be garbage on the right side.
 * \param dbg An optional sink for debugging images.
 * \return A PageLAyout consistent with the layoutType requested.
 */
PageLayout PageLayoutEstimator::cutAtWhitespaceDeskewed150(const LayoutType layoutType,
                                                           const int numPages,
                                                           const BinaryImage& input,
                                                           const bool leftOffcut,
                                                           const bool rightOffcut,
                                                           DebugImages* dbg) {
  const int width = input.width();
  const int height = input.height();

  BinaryImage ccImg(input.size(), WHITE);

  {
    ConnCompEraser ccEraser(input, CONN8);
    ConnComp cc;
    while (!(cc = ccEraser.nextConnComp()).isNull()) {
      if ((cc.width() < 5) || (cc.height() < 5)) {
        continue;
      }
      if ((double) cc.height() / cc.width() > 6) {
        continue;
      }
      ccImg.fill(cc.rect(), BLACK);
    }
  }

  if (dbg) {
    dbg->add(ccImg, "ccImg");
  }

  ContentSpanFinder spanFinder;
  spanFinder.setMinContentWidth(2);
  spanFinder.setMinWhitespaceWidth(8);

  std::deque<Span> spans;
  SlicedHistogram hist(ccImg, SlicedHistogram::COLS);
  spanFinder.find(hist, [&](Span s) { spans.push_back(s); });

  if (dbg) {
    visualizeSpans(*dbg, spans, input, "spans");
  }

  if (numPages == 1) {
    return processContentSpansSinglePage(layoutType, spans, width, height, leftOffcut, rightOffcut);
  } else {
    // This helps if we have 2 pages with one page containing nothing
    // but a small amount of garbage.
    removeInsignificantEdgeSpans(spans);
    if (dbg) {
      visualizeSpans(*dbg, spans, input, "spans_refined");
    }
    return processContentSpansTwoPages(layoutType, spans, width, height);
  }
}  // PageLayoutEstimator::cutAtWhitespaceDeskewed150

imageproc::BinaryImage PageLayoutEstimator::to300DpiBinary(const QImage& img,
                                                           QTransform& xform,
                                                           const BinaryThreshold binaryThreshold) {
  const double xfactor = (300.0 * constants::DPI2DPM) / img.dotsPerMeterX();
  const double yfactor = (300.0 * constants::DPI2DPM) / img.dotsPerMeterY();
  if ((std::fabs(xfactor - 1.0) < 0.1) && (std::fabs(yfactor - 1.0) < 0.1)) {
    return BinaryImage(img, binaryThreshold);
  }

  QTransform scaleXform;
  scaleXform.scale(xfactor, yfactor);
  xform *= scaleXform;
  const QSize newSize(std::max(1, (int) std::ceil(xfactor * img.width())),
                      std::max(1, (int) std::ceil(yfactor * img.height())));

  const GrayImage newImage(scaleToGray(GrayImage(img), newSize));
  return BinaryImage(newImage, binaryThreshold);
}

BinaryImage PageLayoutEstimator::removeGarbageAnd2xDownscale(const BinaryImage& image, DebugImages* dbg) {
  BinaryImage reduced(ReduceThreshold(image)(2));
  if (dbg) {
    dbg->add(reduced, "reduced");
  }
  // Remove anything not connected to a bar of at least 4 pixels long.
  BinaryImage nonGarbageSeed(openBrick(reduced, QSize(4, 1)));
  BinaryImage nonGarbageSeed2(openBrick(reduced, QSize(1, 4)));
  rasterOp<RopOr<RopSrc, RopDst>>(nonGarbageSeed, nonGarbageSeed2);
  nonGarbageSeed2.release();
  reduced = seedFill(nonGarbageSeed, reduced, CONN8);
  nonGarbageSeed.release();

  if (dbg) {
    dbg->add(reduced, "garbage_removed");
  }

  BinaryImage horSeed(openBrick(reduced, QSize(200, 14), BLACK));
  BinaryImage verSeed(openBrick(reduced, QSize(14, 300), BLACK));

  rasterOp<RopOr<RopSrc, RopDst>>(horSeed, verSeed);
  BinaryImage seed(horSeed.release());
  verSeed.release();
  if (dbg) {
    dbg->add(seed, "shadows_seed");
  }

  BinaryImage dilated(dilateBrick(reduced, QSize(3, 3)));

  BinaryImage shadowsDilated(seedFill(seed, dilated, CONN8));
  dilated.release();
  if (dbg) {
    dbg->add(shadowsDilated, "shadowsDilated");
  }

  rasterOp<RopSubtract<RopDst, RopSrc>>(reduced, shadowsDilated);
  return reduced;
}  // PageLayoutEstimator::removeGarbageAnd2xDownscale

bool PageLayoutEstimator::checkForLeftOffcut(const BinaryImage& image) {
  const int margin = 2;  // Some scanners leave garbage near page borders.
  const int width = 3;
  QRect rect(margin, 0, width, image.height());
  rect.adjust(0, margin, 0, -margin);
  return image.countBlackPixels(rect) != 0;
}

bool PageLayoutEstimator::checkForRightOffcut(const BinaryImage& image) {
  const int margin = 2;  // Some scanners leave garbage near page borders.
  const int width = 3;
  QRect rect(image.width() - margin - width, 0, width, image.height());
  rect.adjust(0, margin, 0, -margin);
  return image.countBlackPixels(rect) != 0;
}

void PageLayoutEstimator::visualizeSpans(DebugImages& dbg,
                                         const std::deque<Span>& spans,
                                         const BinaryImage& image,
                                         const char* label) {
  const int height = image.height();

  QImage spansImg(image.toQImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));

  {
    QPainter painter(&spansImg);
    const QBrush brush(QColor(0xff, 0x00, 0x00, 0x50));
    for (const Span& span : spans) {
      const QRect rect(span.begin(), 0, span.width(), height);
      painter.fillRect(rect, brush);
    }
  }
  dbg.add(spansImg, label);
}

void PageLayoutEstimator::removeInsignificantEdgeSpans(std::deque<Span>& spans) {
  if (spans.empty()) {
    return;
  }
  // GapInfo.first: the amount of content preceding this gap.
  // GapInfo.second: the amount of content following this gap.
  using GapInfo = std::pair<int, int>;

  std::vector<GapInfo> gaps(spans.size() - 1);

  int sum = 0;
  for (unsigned i = 0; i < gaps.size(); ++i) {
    sum += spans[i].width();
    gaps[i].first = sum;
  }
  sum = 0;
  for (auto i = static_cast<int>(gaps.size() - 1); i >= 0; --i) {
    sum += spans[i + 1].width();
    gaps[i].second = sum;
  }
  const int total = sum + spans[0].width();

  int mayBeRemoved = total / 15;

  do {
    const Span& first = spans.front();
    const Span& last = spans.back();
    if (&first == &last) {
      break;
    }
    if (first.width() < last.width()) {
      if (first.width() > mayBeRemoved) {
        break;
      }
      mayBeRemoved -= first.width();
      spans.pop_front();
    } else {
      if (last.width() > mayBeRemoved) {
        break;
      }
      mayBeRemoved -= last.width();
      spans.pop_back();
    }
  } while (!spans.empty());
}  // PageLayoutEstimator::removeInsignificantEdgeSpans

PageLayout PageLayoutEstimator::processContentSpansSinglePage(const LayoutType layoutType,
                                                              const std::deque<Span>& spans,
                                                              const int width,
                                                              const int height,
                                                              const bool leftOffcut,
                                                              const bool rightOffcut) {
  assert(layoutType == AUTO_LAYOUT_TYPE || layoutType == PAGE_PLUS_OFFCUT);

  const QRectF virtualImageRect(0, 0, width, height);

  // Just to be able to break from it.
  while (leftOffcut && !rightOffcut && layoutType == AUTO_LAYOUT_TYPE) {
    double x;
    if (spans.empty()) {
      x = 0.0;
    } else if (spans.front().begin() > 0) {
      x = 0.5 * spans.front().begin();
    } else {
      if (spans.front().width() > width / 2) {
        // Probably it's the content span.
        // Maybe we should cut it from the other side.
        break;
      } else if (spans.size() > 1) {
        x = Span(spans[0], spans[1]).center();
      } else {
        x = std::min(spans[0].end() + 20, width);
      }
    }

    const QLineF rightLine(virtualImageRect.topRight(), virtualImageRect.bottomRight());
    return PageLayout(virtualImageRect, vertLine(x), rightLine);
  }

  // Just to be able to break from it.
  while (rightOffcut && !leftOffcut && layoutType == AUTO_LAYOUT_TYPE) {
    double x;
    if (spans.empty()) {
      x = width;
    } else if (spans.back().end() < width) {
      x = Span(spans.back(), width).center();
    } else {
      if (spans.back().width() > width / 2) {
        // Probably it's the content span.
        // Maybe we should cut it from the other side.
        break;
      } else if (spans.size() > 1) {
        x = Span(spans[spans.size() - 2], spans.back()).center();
      } else {
        x = std::max(spans.back().begin() - 20, 0);
      }
    }

    const QLineF leftLine(virtualImageRect.topLeft(), virtualImageRect.bottomLeft());
    return PageLayout(virtualImageRect, leftLine, vertLine(x));
  }

  if (layoutType == PAGE_PLUS_OFFCUT) {
    const QLineF line1(virtualImageRect.topLeft(), virtualImageRect.bottomLeft());
    const QLineF line2(virtualImageRect.topRight(), virtualImageRect.bottomRight());
    return PageLayout(virtualImageRect, line1, line2);
  } else {
    // Returning a SINGLE_PAGE_UNCUT layout.
    return PageLayout(virtualImageRect);
  }
}  // PageLayoutEstimator::processContentSpansSinglePage

PageLayout PageLayoutEstimator::processContentSpansTwoPages(const LayoutType layoutType,
                                                            const std::deque<Span>& spans,
                                                            const int width,
                                                            const int height) {
  assert(layoutType == AUTO_LAYOUT_TYPE || layoutType == TWO_PAGES);

  const QRectF virtualImageRect(0, 0, width, height);

  double x;
  if (spans.empty()) {
    x = 0.5 * width;
  } else if (spans.size() == 1) {
    return processTwoPagesWithSingleSpan(spans.front(), width, height);
  } else {
    // GapInfo.first: the amount of content preceding this gap.
    // GapInfo.second: the amount of content following this gap.
    using GapInfo = std::pair<int, int>;

    std::vector<GapInfo> gaps(spans.size() - 1);
#if 0
            int sum = 0;
            for (unsigned i = 0; i < gaps.size(); ++i) {
                sum += spans[i].width();
                gaps[i].first = sum;
            }
            sum = 0;
            for (int i = gaps.size() - 1; i >= 0; --i) {
                sum += spans[i + 1].width();
                gaps[i].second = sum;
            }
#else
    const int contentBegin = spans.front().begin();
    const int contentEnd = spans.back().end();
    for (unsigned i = 0; i < gaps.size(); ++i) {
      gaps[i].first = spans[i].end() - contentBegin;
      gaps[i].second = contentEnd - spans[i + 1].begin();
    }
#endif

    int bestGap = 0;
    double bestRatio = 0;
    for (unsigned i = 0; i < gaps.size(); ++i) {
      const double min = std::min(gaps[i].first, gaps[i].second);
      const double max = std::max(gaps[i].first, gaps[i].second);
      const double ratio = min / max;
      if (ratio > bestRatio) {
        bestRatio = ratio;
        bestGap = i;
      }
    }

    if (bestRatio < 0.25) {
      // Probably one of the pages is just empty.
      return processTwoPagesWithSingleSpan(Span(contentBegin, contentEnd), width, height);
    }

    const double acceptableRatio = bestRatio * 0.90;

    int widestGap = bestGap;
    int maxWidth = Span(spans[bestGap], spans[bestGap + 1]).width();
    for (int i = bestGap - 1; i >= 0; --i) {
      const double min = std::min(gaps[i].first, gaps[i].second);
      const double max = std::max(gaps[i].first, gaps[i].second);
      const double ratio = min / max;
      if (ratio < acceptableRatio) {
        break;
      }
      const int width = Span(spans[i], spans[i + 1]).width();
      if (width > maxWidth) {
        maxWidth = width;
        widestGap = i;
      }
    }
    for (auto i = static_cast<unsigned int>(bestGap + 1); i < gaps.size(); ++i) {
      const double min = std::min(gaps[i].first, gaps[i].second);
      const double max = std::max(gaps[i].first, gaps[i].second);
      const double ratio = min / max;
      if (ratio < acceptableRatio) {
        break;
      }
      const int width = Span(spans[i], spans[i + 1]).width();
      if (width > maxWidth) {
        maxWidth = width;
        widestGap = i;
      }
    }

    const Span gap(spans[widestGap], spans[widestGap + 1]);
    x = gap.center();
  }
  return PageLayout(virtualImageRect, vertLine(x));
}  // PageLayoutEstimator::processContentSpansTwoPages

PageLayout PageLayoutEstimator::processTwoPagesWithSingleSpan(const Span& span, int width, int height) {
  const QRectF virtualImageRect(0, 0, width, height);

  const double pageCenter = 0.5 * width;
  const double boxCenter = span.center();
  const double boxHalfWidth = 0.5 * span.width();
  const double distanceToPageCenter = std::fabs(pageCenter - boxCenter) - boxHalfWidth;

  double x;

  if (distanceToPageCenter > 15) {
    x = pageCenter;
  } else {
    const Span leftWs(0, span);
    const Span rightWs(span, width);
    if (leftWs.width() > rightWs.width()) {
      x = std::max(0, span.begin() - 15);
    } else {
      x = std::min(width, span.end() + 15);
    }
  }
  return PageLayout(virtualImageRect, vertLine(x));
}

QLineF PageLayoutEstimator::vertLine(double x) {
  return QLineF(x, 0.0, x, 1.0);
}
}  // namespace page_split
