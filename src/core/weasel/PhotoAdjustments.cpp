// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PhotoAdjustments.h"

namespace weasel {

PhotoAdjustments::PhotoAdjustments() = default;

PhotoAdjustments::PhotoAdjustments(const QDomElement& el) {
  if (el.isNull()) return;
  m_temp = std::clamp(el.attribute("temp", "0").toDouble(), -100.0, 100.0);
  m_tint = std::clamp(el.attribute("tint", "0").toDouble(), -100.0, 100.0);
  m_exposure = std::clamp(el.attribute("exposure", "0").toDouble(), MIN_EXPOSURE, MAX_EXPOSURE);
  m_contrast = std::clamp(el.attribute("contrast", "0").toDouble(), -100.0, 100.0);
  m_highlights = std::clamp(el.attribute("highlights", "0").toDouble(), -100.0, 100.0);
  m_shadows = std::clamp(el.attribute("shadows", "0").toDouble(), -100.0, 100.0);
  m_whites = std::clamp(el.attribute("whites", "0").toDouble(), -100.0, 100.0);
  m_blacks = std::clamp(el.attribute("blacks", "0").toDouble(), -100.0, 100.0);
}

QDomElement PhotoAdjustments::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("temp", m_temp);
  el.setAttribute("tint", m_tint);
  el.setAttribute("exposure", m_exposure);
  el.setAttribute("contrast", m_contrast);
  el.setAttribute("highlights", m_highlights);
  el.setAttribute("shadows", m_shadows);
  el.setAttribute("whites", m_whites);
  el.setAttribute("blacks", m_blacks);
  return el;
}

bool PhotoAdjustments::operator==(const PhotoAdjustments& other) const {
  return m_temp == other.m_temp && m_tint == other.m_tint && m_exposure == other.m_exposure
         && m_contrast == other.m_contrast && m_highlights == other.m_highlights
         && m_shadows == other.m_shadows && m_whites == other.m_whites && m_blacks == other.m_blacks;
}

bool PhotoAdjustments::isDefault() const {
  return m_temp == 0.0 && m_tint == 0.0 && m_exposure == 0.0 && m_contrast == 0.0 && m_highlights == 0.0
         && m_shadows == 0.0 && m_whites == 0.0 && m_blacks == 0.0;
}

}  // namespace weasel
