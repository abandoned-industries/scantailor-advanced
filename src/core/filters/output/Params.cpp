// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Params.h"

#include <XmlMarshaller.h>
#include <XmlUnmarshaller.h>
#include <foundation/Utils.h>

using namespace foundation;

namespace output {
Params::Params() : m_dpi(300, 300), m_despeckleLevel(1.0), m_blackOnWhite(true) {}

Params::Params(const Dpi& dpi,
               const ColorParams& colorParams,
               const SplittingOptions& splittingOptions,
               const PictureShapeOptions& pictureShapeOptions,
               const dewarping::DistortionModel& distortionModel,
               const DepthPerception& depthPerception,
               const DewarpingOptions& dewarpingOptions,
               const double despeckleLevel)
    : m_dpi(dpi),
      m_colorParams(colorParams),
      m_splittingOptions(splittingOptions),
      m_pictureShapeOptions(pictureShapeOptions),
      m_distortionModel(distortionModel),
      m_depthPerception(depthPerception),
      m_dewarpingOptions(dewarpingOptions),
      m_despeckleLevel(despeckleLevel),
      m_blackOnWhite(true) {}

Params::Params(const QDomElement& el)
    : m_dpi(el.namedItem("dpi").toElement()),
      m_colorParams(el.namedItem("color-params").toElement()),
      m_splittingOptions(el.namedItem("splitting").toElement()),
      m_pictureShapeOptions(el.namedItem("picture-shape-options").toElement()),
      m_distortionModel(el.namedItem("distortion-model").toElement()),
      m_depthPerception(el.attribute("depthPerception")),
      m_dewarpingOptions(el.namedItem("dewarping-options").toElement()),
      m_despeckleLevel(el.attribute("despeckleLevel").toDouble()),
      m_blackOnWhite(el.attribute("blackOnWhite") == "1") {
  const QDomElement regionsEl(el.namedItem("continuous-tone-regions").toElement());
  if (!regionsEl.isNull()) {
    bool analysisWidthOk = false;
    bool analysisHeightOk = false;
    bool sourceXOk = false;
    bool sourceYOk = false;
    bool sourceWidthOk = false;
    bool sourceHeightOk = false;
    const int analysisWidth = regionsEl.attribute("analysisWidth").toInt(&analysisWidthOk);
    const int analysisHeight = regionsEl.attribute("analysisHeight").toInt(&analysisHeightOk);
    const int sourceX = regionsEl.attribute("sourceX").toInt(&sourceXOk);
    const int sourceY = regionsEl.attribute("sourceY").toInt(&sourceYOk);
    const int sourceWidth = regionsEl.attribute("sourceWidth").toInt(&sourceWidthOk);
    const int sourceHeight = regionsEl.attribute("sourceHeight").toInt(&sourceHeightOk);

    QVector<QRect> bounds;
    for (QDomElement regionEl = regionsEl.firstChildElement("region");
         !regionEl.isNull();
         regionEl = regionEl.nextSiblingElement("region")) {
      bool xOk = false;
      bool yOk = false;
      bool widthOk = false;
      bool heightOk = false;
      const QRect rect(regionEl.attribute("x").toInt(&xOk),
                       regionEl.attribute("y").toInt(&yOk),
                       regionEl.attribute("width").toInt(&widthOk),
                       regionEl.attribute("height").toInt(&heightOk));
      if (xOk && yOk && widthOk && heightOk && !rect.isEmpty()) {
        bounds.push_back(rect);
      }
    }

    if (analysisWidthOk && analysisHeightOk && sourceXOk && sourceYOk
        && sourceWidthOk && sourceHeightOk && analysisWidth > 0 && analysisHeight > 0
        && sourceWidth > 0 && sourceHeight > 0 && !bounds.isEmpty()) {
      m_continuousToneRegions.setAnalysisSize(QSize(analysisWidth, analysisHeight));
      m_continuousToneRegions.setSourceRect(
          QRect(sourceX, sourceY, sourceWidth, sourceHeight));
      m_continuousToneRegions.setBounds(bounds);
    }
  }

  const QDomElement framesEl(el.namedItem("picture-frames").toElement());
  if (!framesEl.isNull()) {
    bool analysisWidthOk = false;
    bool analysisHeightOk = false;
    bool sourceXOk = false;
    bool sourceYOk = false;
    bool sourceWidthOk = false;
    bool sourceHeightOk = false;
    const int analysisWidth = framesEl.attribute("analysisWidth").toInt(&analysisWidthOk);
    const int analysisHeight = framesEl.attribute("analysisHeight").toInt(&analysisHeightOk);
    const int sourceX = framesEl.attribute("sourceX").toInt(&sourceXOk);
    const int sourceY = framesEl.attribute("sourceY").toInt(&sourceYOk);
    const int sourceWidth = framesEl.attribute("sourceWidth").toInt(&sourceWidthOk);
    const int sourceHeight = framesEl.attribute("sourceHeight").toInt(&sourceHeightOk);

    QVector<PictureFrame> frames;
    for (QDomElement frameEl = framesEl.firstChildElement("frame");
         !frameEl.isNull();
         frameEl = frameEl.nextSiblingElement("frame")) {
      bool xOk = false;
      bool yOk = false;
      bool widthOk = false;
      bool heightOk = false;
      PictureFrame frame;
      frame.bounds = QRect(frameEl.attribute("x").toInt(&xOk),
                           frameEl.attribute("y").toInt(&yOk),
                           frameEl.attribute("width").toInt(&widthOk),
                           frameEl.attribute("height").toInt(&heightOk));
      frame.reason = frameEl.attribute("reason");
      if (xOk && yOk && widthOk && heightOk && !frame.bounds.isEmpty()) {
        frames.push_back(frame);
      }
    }

    if (analysisWidthOk && analysisHeightOk && sourceXOk && sourceYOk
        && sourceWidthOk && sourceHeightOk && analysisWidth > 0 && analysisHeight > 0
        && sourceWidth > 0 && sourceHeight > 0 && !frames.isEmpty()) {
      m_pictureFrames.setAnalysisSize(QSize(analysisWidth, analysisHeight));
      m_pictureFrames.setSourceRect(QRect(sourceX, sourceY, sourceWidth, sourceHeight));
      m_pictureFrames.setFrames(frames);
    }
  }
}

QDomElement Params::toXml(QDomDocument& doc, const QString& name) const {
  XmlMarshaller marshaller(doc);

  QDomElement el(doc.createElement(name));
  el.appendChild(m_distortionModel.toXml(doc, "distortion-model"));
  el.appendChild(m_pictureShapeOptions.toXml(doc, "picture-shape-options"));
  el.setAttribute("depthPerception", m_depthPerception.toString());
  el.appendChild(m_dewarpingOptions.toXml(doc, "dewarping-options"));
  el.setAttribute("despeckleLevel", Utils::doubleToString(m_despeckleLevel));
  el.appendChild(m_dpi.toXml(doc, "dpi"));
  el.appendChild(m_colorParams.toXml(doc, "color-params"));
  el.appendChild(m_splittingOptions.toXml(doc, "splitting"));
  el.setAttribute("blackOnWhite", m_blackOnWhite ? "1" : "0");
  if (!m_continuousToneRegions.isEmpty()) {
    QDomElement regionsEl(doc.createElement("continuous-tone-regions"));
    regionsEl.setAttribute("analysisWidth", m_continuousToneRegions.analysisSize().width());
    regionsEl.setAttribute("analysisHeight", m_continuousToneRegions.analysisSize().height());
    regionsEl.setAttribute("sourceX", m_continuousToneRegions.sourceRect().x());
    regionsEl.setAttribute("sourceY", m_continuousToneRegions.sourceRect().y());
    regionsEl.setAttribute("sourceWidth", m_continuousToneRegions.sourceRect().width());
    regionsEl.setAttribute("sourceHeight", m_continuousToneRegions.sourceRect().height());
    for (const QRect& bounds : m_continuousToneRegions.bounds()) {
      QDomElement regionEl(doc.createElement("region"));
      regionEl.setAttribute("x", bounds.x());
      regionEl.setAttribute("y", bounds.y());
      regionEl.setAttribute("width", bounds.width());
      regionEl.setAttribute("height", bounds.height());
      regionsEl.appendChild(regionEl);
    }
    el.appendChild(regionsEl);
  }
  if (!m_pictureFrames.isEmpty()) {
    QDomElement framesEl(doc.createElement("picture-frames"));
    framesEl.setAttribute("analysisWidth", m_pictureFrames.analysisSize().width());
    framesEl.setAttribute("analysisHeight", m_pictureFrames.analysisSize().height());
    framesEl.setAttribute("sourceX", m_pictureFrames.sourceRect().x());
    framesEl.setAttribute("sourceY", m_pictureFrames.sourceRect().y());
    framesEl.setAttribute("sourceWidth", m_pictureFrames.sourceRect().width());
    framesEl.setAttribute("sourceHeight", m_pictureFrames.sourceRect().height());
    for (const PictureFrame& frame : m_pictureFrames.frames()) {
      QDomElement frameEl(doc.createElement("frame"));
      frameEl.setAttribute("x", frame.bounds.x());
      frameEl.setAttribute("y", frame.bounds.y());
      frameEl.setAttribute("width", frame.bounds.width());
      frameEl.setAttribute("height", frame.bounds.height());
      frameEl.setAttribute("reason", frame.reason);
      framesEl.appendChild(frameEl);
    }
    el.appendChild(framesEl);
  }
  return el;
}
}  // namespace output
