// Copyright (C) 2026  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FINALIZE_THUMBNAILCOLORMODEFILTER_H_
#define SCANTAILOR_FINALIZE_THUMBNAILCOLORMODEFILTER_H_

#include "Settings.h"

namespace finalize {

class ThumbnailColorModeFilter {
 public:
  ThumbnailColorModeFilter(bool showBlackAndWhite, bool showGrayscale, bool showMixed, bool showColor)
      : m_showBlackAndWhite(showBlackAndWhite),
        m_showGrayscale(showGrayscale),
        m_showMixed(showMixed),
        m_showColor(showColor) {}

  bool includes(ColorMode mode) const {
    switch (mode) {
      case ColorMode::BlackAndWhite:
        return m_showBlackAndWhite;
      case ColorMode::Grayscale:
        return m_showGrayscale;
      case ColorMode::Mixed:
        return m_showMixed;
      case ColorMode::Color:
        return m_showColor;
    }
    return false;
  }

  bool includesAll() const {
    return m_showBlackAndWhite && m_showGrayscale && m_showMixed && m_showColor;
  }

 private:
  bool m_showBlackAndWhite;
  bool m_showGrayscale;
  bool m_showMixed;
  bool m_showColor;
};

}  // namespace finalize

#endif
