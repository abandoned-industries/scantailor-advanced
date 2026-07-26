#include "PhotoFrameDetector.h"

#include <algorithm>
#include <cmath>

#include <QRectF>

#include "AppleVisionDetector.h"

namespace {

struct LineChoice {
  int position = 0;
  double score = 0.0;
};

struct EdgeRun {
  int fixed = 0;
  int begin = 0;
  int end = 0;
  double strength = 0.0;
};

double overlapArea(const QRect& a, const QRect& b) {
  const QRect intersection = a.intersected(b);
  return intersection.isEmpty()
             ? 0.0
             : static_cast<double>(intersection.width()) * intersection.height();
}

double rectArea(const QRect& rect) {
  return rect.isEmpty() ? 0.0 : static_cast<double>(rect.width()) * rect.height();
}

double lineStrength(const QImage& gray,
                    const bool vertical,
                    const int position,
                    const int rangeBegin,
                    const int rangeEnd) {
  const int fixedLimit = vertical ? gray.width() : gray.height();
  const int variableLimit = vertical ? gray.height() : gray.width();
  if (position < 2 || position >= fixedLimit - 2) {
    return 0.0;
  }
  const int begin = std::clamp(rangeBegin, 0, variableLimit);
  const int end = std::clamp(rangeEnd, 0, variableLimit);
  if (end - begin < 8) {
    return 0.0;
  }

  double sum = 0.0;
  int strong = 0;
  int samples = 0;
  const int step = std::max(1, (end - begin) / 400);
  for (int variable = begin; variable < end; variable += step) {
    const int before = vertical ? gray.constScanLine(variable)[position - 2]
                                : gray.constScanLine(position - 2)[variable];
    const int after = vertical ? gray.constScanLine(variable)[position + 2]
                               : gray.constScanLine(position + 2)[variable];
    const int gradient = std::abs(after - before);
    sum += gradient;
    strong += gradient >= 18;
    ++samples;
  }
  if (samples == 0) {
    return 0.0;
  }
  const double mean = sum / samples;
  const double continuity = static_cast<double>(strong) / samples;
  return std::min(1.0, (mean + 55.0 * continuity) / 110.0);
}

LineChoice chooseLine(const QImage& gray,
                      const bool vertical,
                      const int expected,
                      const int radius,
                      const int spanBegin,
                      const int spanEnd) {
  LineChoice best{expected, 0.0};
  const int limit = vertical ? gray.width() : gray.height();
  const int begin = std::max(2, expected - radius);
  const int end = std::min(limit - 3, expected + radius);
  for (int position = begin; position <= end; ++position) {
    const double raw = lineStrength(gray, vertical, position, spanBegin, spanEnd);
    const double proximity =
        1.0 - 0.12 * std::abs(position - expected) / std::max(1, radius);
    const double score = raw * proximity;
    if (score > best.score) {
      best = {position, score};
    }
  }
  return best;
}

PhotoFrameDetector::Candidate snapCandidate(const QImage& gray,
                                            const QRect& seed,
                                            const int radiusX,
                                            const int radiusY,
                                            const QString& provenance,
                                            const double confidence) {
  const QRect clipped = seed.intersected(gray.rect());
  PhotoFrameDetector::Candidate result;
  result.bounds = clipped;
  result.provenance = provenance;
  result.proposalConfidence = confidence;
  if (clipped.isEmpty()) {
    return result;
  }

  LineChoice left = chooseLine(gray, true, clipped.left(), radiusX,
                               clipped.top(), clipped.bottom() + 1);
  LineChoice right = chooseLine(gray, true, clipped.right(), radiusX,
                                clipped.top(), clipped.bottom() + 1);
  if (right.position - left.position < gray.width() * 0.06) {
    left.position = clipped.left();
    right.position = clipped.right();
  }
  LineChoice top = chooseLine(gray, false, clipped.top(), radiusY,
                              left.position, right.position + 1);
  LineChoice bottom = chooseLine(gray, false, clipped.bottom(), radiusY,
                                 left.position, right.position + 1);
  if (bottom.position - top.position < gray.height() * 0.06) {
    top.position = clipped.top();
    bottom.position = clipped.bottom();
  }

  result.bounds = QRect(QPoint(left.position, top.position),
                        QPoint(right.position, bottom.position))
                      .normalized()
                      .intersected(gray.rect());
  result.edgeScore = (left.score + right.score + top.score + bottom.score) / 4.0;
  return result;
}

double tileWeightInside(const QRect& rect,
                        const LeptonicaDetector::ToneTileEvidence& tile) {
  const QRect tileRect(tile.x, tile.y, tile.width, tile.height);
  return overlapArea(rect, tileRect) / std::max(1.0, rectArea(tileRect));
}

bool substantiallySame(const QRect& a, const QRect& b) {
  const double intersection = overlapArea(a, b);
  return intersection / std::max(1.0, std::min(rectArea(a), rectArea(b))) >= 0.82;
}

QVector<EdgeRun> extractRuns(const QImage& gray, const bool horizontal) {
  QVector<EdgeRun> runs;
  const int fixedLimit = horizontal ? gray.height() : gray.width();
  const int variableLimit = horizontal ? gray.width() : gray.height();
  const int minimumLength = std::max(24, variableLimit * 11 / 100);
  const int maxGap = std::max(2, variableLimit / 240);
  for (int fixed = 2; fixed < fixedLimit - 2; ++fixed) {
    int runBegin = -1;
    int lastStrong = -1;
    double gradientSum = 0.0;
    int gradientCount = 0;
    for (int variable = 0; variable < variableLimit; ++variable) {
      const int before = horizontal ? gray.constScanLine(fixed - 2)[variable]
                                    : gray.constScanLine(variable)[fixed - 2];
      const int after = horizontal ? gray.constScanLine(fixed + 2)[variable]
                                   : gray.constScanLine(variable)[fixed + 2];
      const int gradient = std::abs(after - before);
      if (gradient >= 16) {
        if (runBegin < 0) {
          runBegin = variable;
        }
        lastStrong = variable;
        gradientSum += gradient;
        ++gradientCount;
      } else if (runBegin >= 0 && variable - lastStrong > maxGap) {
        if (lastStrong - runBegin + 1 >= minimumLength) {
          runs.push_back({fixed, runBegin, lastStrong,
                          std::min(1.0, gradientSum
                                            / std::max(1, gradientCount) / 90.0)});
        }
        runBegin = -1;
        lastStrong = -1;
        gradientSum = 0.0;
        gradientCount = 0;
      }
    }
    if (runBegin >= 0 && lastStrong - runBegin + 1 >= minimumLength) {
      runs.push_back({fixed, runBegin, lastStrong,
                      std::min(1.0, gradientSum
                                        / std::max(1, gradientCount) / 90.0)});
    }
  }

  // Collapse the several adjacent scanlines generated by one printed edge.
  QVector<EdgeRun> collapsed;
  std::sort(runs.begin(), runs.end(), [](const EdgeRun& a, const EdgeRun& b) {
    if (a.fixed != b.fixed) {
      return a.fixed < b.fixed;
    }
    return a.strength > b.strength;
  });
  for (const EdgeRun& run : runs) {
    bool merged = false;
    for (int i = collapsed.size() - 1; i >= 0; --i) {
      EdgeRun& prior = collapsed[i];
      if (run.fixed - prior.fixed > 3) {
        break;
      }
      const int overlap = std::min(run.end, prior.end) - std::max(run.begin, prior.begin) + 1;
      const int shorter = std::min(run.end - run.begin + 1, prior.end - prior.begin + 1);
      if (overlap > shorter * 0.65) {
        if (run.strength > prior.strength) {
          prior = run;
        }
        merged = true;
        break;
      }
    }
    if (!merged) {
      collapsed.push_back(run);
    }
  }
  std::sort(collapsed.begin(), collapsed.end(), [](const EdgeRun& a, const EdgeRun& b) {
    const double aRank = (a.end - a.begin + 1) * a.strength;
    const double bRank = (b.end - b.begin + 1) * b.strength;
    return aRank > bRank;
  });
  if (collapsed.size() > 48) {
    collapsed.resize(48);
  }
  return collapsed;
}

QVector<PhotoFrameDetector::Candidate> assembleCvRectangles(const QImage& gray) {
  const QVector<EdgeRun> horizontal = extractRuns(gray, true);
  const QVector<EdgeRun> vertical = extractRuns(gray, false);
  QVector<PhotoFrameDetector::Candidate> candidates;
  const int minWidth = gray.width() * 7 / 100;
  const int minHeight = gray.height() * 7 / 100;
  const int joinTolerance = std::max(8, std::min(gray.width(), gray.height()) / 60);

  for (int topIndex = 0; topIndex < horizontal.size(); ++topIndex) {
    for (int bottomIndex = topIndex + 1; bottomIndex < horizontal.size(); ++bottomIndex) {
      const EdgeRun& first = horizontal[topIndex];
      const EdgeRun& second = horizontal[bottomIndex];
      const EdgeRun& top = first.fixed < second.fixed ? first : second;
      const EdgeRun& bottom = first.fixed < second.fixed ? second : first;
      if (bottom.fixed - top.fixed < minHeight) {
        continue;
      }
      const int horizontalBegin = std::max(top.begin, bottom.begin);
      const int horizontalEnd = std::min(top.end, bottom.end);
      if (horizontalEnd - horizontalBegin < minWidth) {
        continue;
      }
      for (int leftIndex = 0; leftIndex < vertical.size(); ++leftIndex) {
        for (int rightIndex = leftIndex + 1; rightIndex < vertical.size(); ++rightIndex) {
          const EdgeRun& firstVertical = vertical[leftIndex];
          const EdgeRun& secondVertical = vertical[rightIndex];
          const EdgeRun& left =
              firstVertical.fixed < secondVertical.fixed ? firstVertical : secondVertical;
          const EdgeRun& right =
              firstVertical.fixed < secondVertical.fixed ? secondVertical : firstVertical;
          if (right.fixed - left.fixed < minWidth
              || left.fixed < horizontalBegin - joinTolerance
              || right.fixed > horizontalEnd + joinTolerance
              || left.end < top.fixed - joinTolerance
              || left.begin > bottom.fixed + joinTolerance
              || right.end < top.fixed - joinTolerance
              || right.begin > bottom.fixed + joinTolerance) {
            continue;
          }
          PhotoFrameDetector::Candidate candidate;
          candidate.bounds = QRect(QPoint(left.fixed, top.fixed),
                                   QPoint(right.fixed, bottom.fixed));
          candidate.edgeScore =
              (top.strength + bottom.strength + left.strength + right.strength) / 4.0;
          candidate.proposalConfidence = candidate.edgeScore;
          candidate.provenance = QStringLiteral("cv_edge_runs_rectangle");
          bool duplicate = false;
          for (const auto& existing : candidates) {
            if (substantiallySame(existing.bounds, candidate.bounds)) {
              duplicate = true;
              break;
            }
          }
          if (!duplicate) {
            candidates.push_back(candidate);
          }
          if (candidates.size() >= 96) {
            return candidates;
          }
        }
      }
    }
  }
  return candidates;
}

}  // namespace

PhotoFrameDetector::Result PhotoFrameDetector::detect(
    const QImage& detectionCrop,
    const LeptonicaDetector::DetectionEvidence& evidence) {
  Result result;
  if (detectionCrop.isNull() || !evidence.analysisSize.isValid()) {
    return result;
  }

  const QImage analysis =
      detectionCrop.scaled(evidence.analysisSize, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
  const QImage gray = analysis.convertToFormat(QImage::Format_Grayscale8);

  for (const AppleVisionDetector::RectangleResult& rectangle :
       AppleVisionDetector::detectRectangles(analysis)) {
    const QRect seed = rectangle.bounds.toAlignedRect().intersected(analysis.rect());
    Candidate candidate = snapCandidate(
        gray, seed, std::max(4, analysis.width() / 40),
        std::max(4, analysis.height() / 40), QStringLiteral("vision+gradient_snap"),
        rectangle.confidence);
    if (!candidate.bounds.isEmpty()) {
      result.visionCandidates.push_back(candidate);
    }
  }
  const double analysisArea =
      static_cast<double>(analysis.width()) * analysis.height();
  const bool hasLargeVisionFrame =
      std::any_of(result.visionCandidates.begin(), result.visionCandidates.end(),
                  [analysisArea](const Candidate& candidate) {
                    return rectArea(candidate.bounds) / std::max(1.0, analysisArea) >= 0.04;
                  });

  for (const Candidate& assembled : assembleCvRectangles(gray)) {
    Candidate snapped = snapCandidate(
        gray, assembled.bounds, std::max(3, analysis.width() / 100),
        std::max(3, analysis.height() / 100), assembled.provenance,
        assembled.proposalConfidence);
    if (!snapped.bounds.isEmpty()) {
      result.cvCandidates.push_back(snapped);
    }
  }

  const QVector<QRect> toneRegions = evidence.regionBounds.isEmpty()
                                         ? QVector<QRect>{evidence.largestRegionBounds}
                                         : evidence.regionBounds;
  QVector<Candidate> evidenceRefinements;
  for (const QRect& region : toneRegions) {
    if (region.isEmpty()) {
      continue;
    }
    Candidate candidate = snapCandidate(
        gray, region,
        std::max(8, analysis.width() / (hasLargeVisionFrame ? 9 : 2)),
        std::max(8, analysis.height() / (hasLargeVisionFrame ? 9 : 2)),
        QStringLiteral("cv_gradient_from_tone_region"),
        1.0);
    if (!candidate.bounds.isEmpty()) {
      evidenceRefinements.push_back(candidate);
    }
  }

  QVector<Candidate> combined = result.visionCandidates;
  for (const Candidate& cv : result.cvCandidates) {
    bool duplicate = false;
    for (Candidate& existing : combined) {
      if (substantiallySame(existing.bounds, cv.bounds)) {
        const double areaRatio =
            std::min(rectArea(existing.bounds), rectArea(cv.bounds))
            / std::max(1.0, std::max(rectArea(existing.bounds), rectArea(cv.bounds)));
        if (areaRatio >= 0.70 && cv.edgeScore > existing.edgeScore) {
          existing.bounds = cv.bounds;
          existing.edgeScore = cv.edgeScore;
        } else if (rectArea(cv.bounds) > rectArea(existing.bounds)) {
          existing = cv;
        }
        existing.provenance = QStringLiteral("vision+cv_gradient_snap");
        existing.proposalConfidence =
            std::max(existing.proposalConfidence, cv.proposalConfidence);
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      combined.push_back(cv);
    }
  }
  for (const Candidate& refinement : evidenceRefinements) {
    bool duplicate = false;
    for (Candidate& existing : combined) {
      if (substantiallySame(existing.bounds, refinement.bounds)) {
        const double areaRatio =
            std::min(rectArea(existing.bounds), rectArea(refinement.bounds))
            / std::max(1.0, std::max(rectArea(existing.bounds), rectArea(refinement.bounds)));
        if (areaRatio >= 0.70 && refinement.edgeScore > existing.edgeScore) {
          existing.bounds = refinement.bounds;
          existing.edgeScore = refinement.edgeScore;
        }
        existing.provenance += QStringLiteral("+tone_seed_refine");
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      combined.push_back(refinement);
    }
  }
  result.acceptedFrames = acceptCandidates(evidence.analysisSize, evidence, combined);

  // A single photograph with strong interior dividers can assemble as several
  // mutually exclusive CV rectangles when Vision misses its outer silhouette.
  // Treat three or more accepted fragments as one outer-frame hypothesis and
  // re-snap their union.  If the outer frame cannot pass the same evidence
  // gates, discard the fragments and leave the existing automask as fallback.
  if (!hasLargeVisionFrame && result.acceptedFrames.size() >= 3) {
    QRect fragmentUnion;
    for (const AcceptedFrame& frame : result.acceptedFrames) {
      fragmentUnion = fragmentUnion.united(frame.bounds);
    }
    const Candidate recovered = snapCandidate(
        gray, fragmentUnion, std::max(4, analysis.width() / 150),
        std::max(4, analysis.height() / 150),
        QStringLiteral("cv_fragment_union+gradient_snap"), 1.0);
    const double retainedUnion =
        overlapArea(recovered.bounds, fragmentUnion)
        / std::max(1.0, rectArea(fragmentUnion));
    result.acceptedFrames =
        retainedUnion >= 0.90
            ? acceptCandidates(evidence.analysisSize, evidence, {recovered})
            : QVector<AcceptedFrame>();
  }
  return result;
}

QVector<PhotoFrameDetector::AcceptedFrame> PhotoFrameDetector::acceptCandidates(
    const QSize& analysisSize,
    const LeptonicaDetector::DetectionEvidence& evidence,
    const QVector<Candidate>& candidates) {
  QVector<AcceptedFrame> accepted;
  if (!analysisSize.isValid()) {
    return accepted;
  }
  const QRect pageRect(QPoint(), analysisSize);
  const double pageArea = rectArea(pageRect);
  const QVector<QRect> toneRegions = evidence.regionBounds.isEmpty()
                                         ? QVector<QRect>{evidence.largestRegionBounds}
                                         : evidence.regionBounds;

  QVector<Candidate> ordered = candidates;
  std::sort(ordered.begin(), ordered.end(), [](const Candidate& a, const Candidate& b) {
    return rectArea(a.bounds) > rectArea(b.bounds);
  });

  for (const Candidate& candidate : ordered) {
    const QRect rect = candidate.bounds.intersected(pageRect);
    const double areaFraction = rectArea(rect) / std::max(1.0, pageArea);
    const double aspect = static_cast<double>(rect.width()) / std::max(1, rect.height());
    if (rect.width() < analysisSize.width() * 0.07
        || rect.height() < analysisSize.height() * 0.07
        || aspect < 0.30 || aspect > 5.0
        || areaFraction < 0.012 || areaFraction > 0.78) {
      continue;
    }

    double toneOverlap = 0.0;
    for (const QRect& region : toneRegions) {
      toneOverlap = std::max(
          toneOverlap,
          overlapArea(rect, region) / std::max(1.0, std::min(rectArea(rect), rectArea(region))));
    }
    if (toneOverlap < 0.30) {
      continue;
    }

    double totalTileWeight = 0.0;
    double continuousToneWeight = 0.0;
    double toneFeatureWeight = 0.0;
    double lineArtWeight = 0.0;
    for (const auto& tile : evidence.toneTiles) {
      const double weight = tileWeightInside(rect, tile);
      totalTileWeight += weight;
      if (tile.continuousTone) {
        continuousToneWeight += weight;
      }
      if (tile.histogramEntropy >= 0.62f
          && tile.blurredEntropy >= 0.55f
          && tile.occupiedBins >= 7
          && tile.lowFrequencyVariance >= 0.012f
          && tile.broadMidtoneRatio >= 0.05f
          && tile.broadMidtoneRatio <= 0.76f) {
        toneFeatureWeight += weight;
      }
      if (tile.lineArtRejected) {
        lineArtWeight += weight;
      }
    }
    const double toneFraction =
        continuousToneWeight / std::max(0.001, totalTileWeight);
    const double toneFeatureFraction =
        toneFeatureWeight / std::max(0.001, totalTileWeight);
    const double lineArtFraction = lineArtWeight / std::max(0.001, totalTileWeight);
    const bool tileToneSupport =
        std::max(continuousToneWeight, toneFeatureWeight) >= 0.10
        && std::max(toneFraction, toneFeatureFraction) >= 0.03;
    const bool strongVisionFrameSupport =
        candidate.provenance.startsWith(QStringLiteral("vision"))
        && toneOverlap >= 0.50 && candidate.edgeScore >= 0.20;
    const bool strongOuterFrameSupport =
        ((candidate.provenance.startsWith(QStringLiteral("vision+cv"))
          && toneOverlap >= 0.30)
         || candidate.provenance.startsWith(QStringLiteral("cv_fragment_union")))
        && areaFraction >= 0.12 && candidate.edgeScore >= 0.50;
    if ((!tileToneSupport && !strongVisionFrameSupport)
        || candidate.edgeScore < 0.075
        || (lineArtFraction > 0.60 && !strongOuterFrameSupport)) {
      continue;
    }

    bool nestedOrDuplicate = false;
    for (const AcceptedFrame& existing : accepted) {
      const double overlapFraction =
          overlapArea(existing.bounds, rect)
          / std::max(1.0, std::min(rectArea(existing.bounds), rectArea(rect)));
      if (overlapFraction >= 0.05) {
        nestedOrDuplicate = true;  // Area order preserves the outermost frame.
        break;
      }
    }
    if (nestedOrDuplicate) {
      continue;
    }

    AcceptedFrame frame;
    frame.bounds = rect;
    frame.reason =
        QStringLiteral("%1;continuous_tone_tiles=%2;tone_features=%3;line_art=%4;edge=%5")
            .arg(candidate.provenance)
            .arg(toneFraction, 0, 'f', 3)
            .arg(toneFeatureFraction, 0, 'f', 3)
            .arg(lineArtFraction, 0, 'f', 3)
            .arg(candidate.edgeScore, 0, 'f', 3);
    accepted.push_back(frame);
  }
  return accepted;
}
