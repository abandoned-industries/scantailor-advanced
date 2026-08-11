// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_WEASEL_PHOTOADJUSTMENTS_H_
#define SCANTAILOR_WEASEL_PHOTOADJUSTMENTS_H_

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <algorithm>
#include <cmath>

namespace weasel {

class PhotoAdjustments {
 public:
  static constexpr double MIN_EXPOSURE = -1.0;
  static constexpr double MAX_EXPOSURE = 1.0;

  PhotoAdjustments();
  explicit PhotoAdjustments(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  bool operator==(const PhotoAdjustments& other) const;
  bool operator!=(const PhotoAdjustments& other) const { return !(*this == other); }

  bool isDefault() const;

  // White Balance
  double temp() const { return m_temp; }
  void setTemp(double v) { m_temp = std::clamp(v, -100.0, 100.0); }

  double tint() const { return m_tint; }
  void setTint(double v) { m_tint = std::clamp(v, -100.0, 100.0); }

  // Tone
  double exposure() const { return m_exposure; }
  void setExposure(double v) { m_exposure = std::clamp(v, MIN_EXPOSURE, MAX_EXPOSURE); }

  double contrast() const { return m_contrast; }
  void setContrast(double v) { m_contrast = std::clamp(v, -100.0, 100.0); }

  // Detail
  double highlights() const { return m_highlights; }
  void setHighlights(double v) { m_highlights = std::clamp(v, -100.0, 100.0); }

  double shadows() const { return m_shadows; }
  void setShadows(double v) { m_shadows = std::clamp(v, -100.0, 100.0); }

  double whites() const { return m_whites; }
  void setWhites(double v) { m_whites = std::clamp(v, -100.0, 100.0); }

  double blacks() const { return m_blacks; }
  void setBlacks(double v) { m_blacks = std::clamp(v, -100.0, 100.0); }

 private:
  double m_temp = 0.0;
  double m_tint = 0.0;
  double m_exposure = 0.0;
  double m_contrast = 0.0;
  double m_highlights = 0.0;
  double m_shadows = 0.0;
  double m_whites = 0.0;
  double m_blacks = 0.0;
};

}  // namespace weasel

#endif  // SCANTAILOR_WEASEL_PHOTOADJUSTMENTS_H_
