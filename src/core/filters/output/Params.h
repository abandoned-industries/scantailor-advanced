// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_PARAMS_H_
#define SCANTAILOR_OUTPUT_PARAMS_H_

#include <dewarping/DistortionModel.h>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

#include "ColorParams.h"
#include "DepthPerception.h"
#include "DespeckleLevel.h"
#include "DewarpingOptions.h"
#include "Dpi.h"
#include "PictureShapeOptions.h"

class QDomDocument;
class QDomElement;

namespace output {
class ContinuousToneRegions {
 public:
  const QSize& analysisSize() const { return m_analysisSize; }
  void setAnalysisSize(const QSize& size) { m_analysisSize = size; }

  const QRect& sourceRect() const { return m_sourceRect; }
  void setSourceRect(const QRect& rect) { m_sourceRect = rect; }

  const QVector<QRect>& bounds() const { return m_bounds; }
  void setBounds(const QVector<QRect>& bounds) { m_bounds = bounds; }

  bool isEmpty() const {
    return !m_analysisSize.isValid() || m_sourceRect.isEmpty() || m_bounds.isEmpty();
  }

  bool operator==(const ContinuousToneRegions& other) const {
    return m_analysisSize == other.m_analysisSize
           && m_sourceRect == other.m_sourceRect
           && m_bounds == other.m_bounds;
  }

  bool operator!=(const ContinuousToneRegions& other) const { return !(*this == other); }

 private:
  QSize m_analysisSize;
  QRect m_sourceRect;
  QVector<QRect> m_bounds;
};

struct PictureFrame {
  QRect bounds;
  QString reason;

  bool operator==(const PictureFrame& other) const {
    return bounds == other.bounds && reason == other.reason;
  }
};

class PictureFrames {
 public:
  const QSize& analysisSize() const { return m_analysisSize; }
  void setAnalysisSize(const QSize& size) { m_analysisSize = size; }

  const QRect& sourceRect() const { return m_sourceRect; }
  void setSourceRect(const QRect& rect) { m_sourceRect = rect; }

  const QVector<PictureFrame>& frames() const { return m_frames; }
  void setFrames(const QVector<PictureFrame>& frames) { m_frames = frames; }

  bool isEmpty() const {
    return !m_analysisSize.isValid() || m_sourceRect.isEmpty() || m_frames.isEmpty();
  }

  bool operator==(const PictureFrames& other) const {
    return m_analysisSize == other.m_analysisSize
           && m_sourceRect == other.m_sourceRect
           && m_frames == other.m_frames;
  }

  bool operator!=(const PictureFrames& other) const { return !(*this == other); }

 private:
  QSize m_analysisSize;
  QRect m_sourceRect;
  QVector<PictureFrame> m_frames;
};

class Params {
 public:
  Params();

  Params(const Dpi& dpi,
         const ColorParams& colorParams,
         const SplittingOptions& splittingOptions,
         const PictureShapeOptions& pictureShapeOptions,
         const dewarping::DistortionModel& distortionModel,
         const DepthPerception& depthPerception,
         const DewarpingOptions& dewarpingOptions,
         double despeckleLevel);

  explicit Params(const QDomElement& el);

  const Dpi& outputDpi() const;

  void setOutputDpi(const Dpi& dpi);

  const ColorParams& colorParams() const;

  const PictureShapeOptions& pictureShapeOptions() const;

  void setPictureShapeOptions(const PictureShapeOptions& opt);

  void setColorParams(const ColorParams& params);

  const SplittingOptions& splittingOptions() const;

  void setSplittingOptions(const SplittingOptions& opt);

  const DewarpingOptions& dewarpingOptions() const;

  void setDewarpingOptions(const DewarpingOptions& opt);

  const dewarping::DistortionModel& distortionModel() const;

  void setDistortionModel(const dewarping::DistortionModel& model);

  const DepthPerception& depthPerception() const;

  void setDepthPerception(DepthPerception depthPerception);

  double despeckleLevel() const;

  void setDespeckleLevel(double level);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  bool isBlackOnWhite() const;

  void setBlackOnWhite(bool isBlackOnWhite);

  const ContinuousToneRegions& continuousToneRegions() const;

  void setContinuousToneRegions(const ContinuousToneRegions& regions);

  const PictureFrames& pictureFrames() const;

  void setPictureFrames(const PictureFrames& frames);

 private:
  Dpi m_dpi;
  ColorParams m_colorParams;
  SplittingOptions m_splittingOptions;
  PictureShapeOptions m_pictureShapeOptions;
  dewarping::DistortionModel m_distortionModel;
  DepthPerception m_depthPerception;
  DewarpingOptions m_dewarpingOptions;
  double m_despeckleLevel;
  bool m_blackOnWhite;
  ContinuousToneRegions m_continuousToneRegions;
  PictureFrames m_pictureFrames;
};


inline const Dpi& Params::outputDpi() const {
  return m_dpi;
}

inline void Params::setOutputDpi(const Dpi& dpi) {
  m_dpi = dpi;
}

inline const ColorParams& Params::colorParams() const {
  return m_colorParams;
}

inline const PictureShapeOptions& Params::pictureShapeOptions() const {
  return m_pictureShapeOptions;
}

inline void Params::setPictureShapeOptions(const PictureShapeOptions& opt) {
  m_pictureShapeOptions = opt;
}

inline void Params::setColorParams(const ColorParams& params) {
  m_colorParams = params;
}

inline const SplittingOptions& Params::splittingOptions() const {
  return m_splittingOptions;
}

inline void Params::setSplittingOptions(const SplittingOptions& opt) {
  m_splittingOptions = opt;
}

inline const DewarpingOptions& Params::dewarpingOptions() const {
  return m_dewarpingOptions;
}

inline void Params::setDewarpingOptions(const DewarpingOptions& opt) {
  m_dewarpingOptions = opt;
}

inline const dewarping::DistortionModel& Params::distortionModel() const {
  return m_distortionModel;
}

inline void Params::setDistortionModel(const dewarping::DistortionModel& model) {
  m_distortionModel = model;
}

inline const DepthPerception& Params::depthPerception() const {
  return m_depthPerception;
}

inline void Params::setDepthPerception(DepthPerception depthPerception) {
  m_depthPerception = depthPerception;
}

inline double Params::despeckleLevel() const {
  return m_despeckleLevel;
}

inline void Params::setDespeckleLevel(double level) {
  m_despeckleLevel = level;
}

inline bool Params::isBlackOnWhite() const {
  return m_blackOnWhite;
}

inline void Params::setBlackOnWhite(bool isBlackOnWhite) {
  Params::m_blackOnWhite = isBlackOnWhite;
}

inline const ContinuousToneRegions& Params::continuousToneRegions() const {
  return m_continuousToneRegions;
}

inline void Params::setContinuousToneRegions(const ContinuousToneRegions& regions) {
  m_continuousToneRegions = regions;
}

inline const PictureFrames& Params::pictureFrames() const {
  return m_pictureFrames;
}

inline void Params::setPictureFrames(const PictureFrames& frames) {
  m_pictureFrames = frames;
}
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_PARAMS_H_
