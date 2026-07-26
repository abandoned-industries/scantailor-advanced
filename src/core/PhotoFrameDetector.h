#ifndef SCANTAILOR_CORE_PHOTOFRAMEDETECTOR_H_
#define SCANTAILOR_CORE_PHOTOFRAMEDETECTOR_H_

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

#include "LeptonicaDetector.h"

class PhotoFrameDetector {
 public:
  struct Candidate {
    QRect bounds;
    double edgeScore = 0.0;
    double proposalConfidence = 0.0;
    QString provenance;
  };

  struct AcceptedFrame {
    QRect bounds;
    QString reason;

    bool operator==(const AcceptedFrame& other) const {
      return bounds == other.bounds && reason == other.reason;
    }
  };

  struct Result {
    QVector<Candidate> visionCandidates;
    QVector<Candidate> cvCandidates;
    QVector<AcceptedFrame> acceptedFrames;
  };

  /**
   * Run Vision rectangle proposals and deterministic gradient snapping on the
   * same bounded analysis raster used by DetectionEvidence.
   */
  static Result detect(const QImage& detectionCrop,
                       const LeptonicaDetector::DetectionEvidence& evidence);

  /**
   * Pure candidate acceptance seam used by focused synthetic tests.
   */
  static QVector<AcceptedFrame> acceptCandidates(
      const QSize& analysisSize,
      const LeptonicaDetector::DetectionEvidence& evidence,
      const QVector<Candidate>& candidates);
};

#endif  // SCANTAILOR_CORE_PHOTOFRAMEDETECTOR_H_
