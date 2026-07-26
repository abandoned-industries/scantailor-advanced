// Headless production-geometry diagnostic for representative PDF pages.

#include <AppleVisionDetector.h>
#include <BinaryThreshold.h>
#include <FilterData.h>
#include <Grayscale.h>
#include <ImageSettings.h>
#include <NullTaskStatus.h>
#include <PdfReader.h>
#include <PlatePrior.h>
#include <filters/deskew/Task.h>
#include <filters/page_split/PageLayout.h>
#include <filters/page_split/PageLayoutEstimator.h>
#include <filters/select_content/ContentBoxFinder.h>
#include <filters/select_content/Settings.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QGuiApplication>
#include <QPainter>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct Options {
  QString input;
  QString output;
  int dpi = 300;
  std::vector<int> pages;
};

bool parse(const QStringList& args, Options* out) {
  if (args.size() < 4) return false;
  out->input = args[1];
  out->output = args[2];
  for (int i = 3; i < args.size(); ++i) {
    if (args[i].startsWith("--dpi=")) {
      out->dpi = args[i].mid(6).toInt();
    } else {
      bool ok = false;
      const int page = args[i].toInt(&ok);
      if (!ok || page < 0) return false;
      out->pages.push_back(page);
    }
  }
  return out->dpi > 0 && !out->pages.empty();
}

QString layoutName(const page_split::PageLayout& layout) {
  switch (layout.type()) {
    case page_split::PageLayout::SINGLE_PAGE_UNCUT: return "single_uncut";
    case page_split::PageLayout::SINGLE_PAGE_CUT: return "single_cut";
    case page_split::PageLayout::TWO_PAGES: return "two_pages";
  }
  return "unknown";
}

QImage visionInput(const QImage& image) {
  constexpr int maxDimension = 1800;
  if (std::max(image.width(), image.height()) <= maxDimension) return image;
  return image.scaled(maxDimension, maxDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString csvQuote(QString value) {
  value.replace('"', QStringLiteral("\"\""));
  return QStringLiteral("\"%1\"").arg(value);
}

QImage makeOverlay(const QImage& source,
                   const page_split::PageLayout& layout,
                   double angle,
                   const QRectF& content,
                   const QString& caption) {
  QImage overlay = source.scaled(900, 900, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_RGB32);
  const double sx = double(overlay.width()) / source.width();
  const double sy = double(overlay.height()) / source.height();
  QPainter painter(&overlay);
  painter.setRenderHint(QPainter::Antialiasing);
  if (layout.numCutters() > 0) {
    const QLineF line = layout.inscribedCutterLine(0);
    painter.setPen(QPen(QColor(255, 45, 45), 5));
    painter.drawLine(QPointF(line.x1() * sx, line.y1() * sy), QPointF(line.x2() * sx, line.y2() * sy));
  }
  painter.setPen(QPen(QColor(40, 220, 80), 5));
  painter.drawRect(QRectF(content.x() * sx, content.y() * sy, content.width() * sx, content.height() * sy));
  const QPointF center(overlay.width() / 2.0, overlay.height() / 2.0);
  const double radians = angle * 3.14159265358979323846 / 180.0;
  const double half = overlay.width() * 0.42;
  const QPointF delta(half * std::cos(radians), half * std::sin(radians));
  painter.setPen(QPen(QColor(40, 120, 255), 4));
  painter.drawLine(center - delta, center + delta);
  painter.fillRect(QRect(0, 0, overlay.width(), 35), QColor(0, 0, 0, 190));
  painter.setPen(Qt::white);
  painter.drawText(QRect(8, 0, overlay.width() - 16, 35), Qt::AlignVCenter, caption);
  return overlay;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  Options options;
  if (!parse(app.arguments(), &options)) {
    std::fprintf(stderr, "usage: %s INPUT_PDF OUTPUT_DIR PAGE_INDEX... [--dpi=N]\n", argv[0]);
    return 2;
  }
  QDir().mkpath(options.output);
  QFile csv(options.output + "/geometry.csv");
  if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) return 1;
  QTextStream stream(&csv);
  stream << "page,width,height,vision_text,vision_left,vision_right,vision_should_split,"
            "vision_split,vision_confidence,vision_text_area_fraction,vision_text_positions,"
            "vision_full_text,vision_full_text_area_fraction,"
            "caption_scale,caption_peripheral,caption_interior_area,spread_geometry,"
            "spread_aspect,spread_gutter,spread_gutter_center,spread_gutter_width,"
            "layout,split_x,split_decision_reason,deskew_angle,"
            "content_x,content_y,content_width,content_height,content_fraction,"
            "plate_prior,plate_entropy,plate_tonal_tiles,plate_midtones,plate_reason,overlay\n";

  NullTaskStatus status;
  std::vector<QImage> overlays;
  for (int page : options.pages) {
    QImage image = PdfReader::readImage(options.input, page, options.dpi);
    if (image.isNull()) continue;
    FilterData data(image);
    ImageSettings::PageParams imageParams(
        imageproc::BinaryThreshold::otsuThreshold(imageproc::GrayscaleHistogram(data.grayImage())), true);
    data.updateImageParams(imageParams);

    const QImage vision = visionInput(image);
    const auto text = AppleVisionDetector::detectTextRegions(vision);
    const auto fullText = (vision.size() == image.size())
        ? text : AppleVisionDetector::detectTextRegions(image);
    double textAreaFraction = 0.0;
    QStringList textPositions;
    for (const auto& region : text) {
      const QRectF bounds(region.bounds.x() / vision.width(),
                          region.bounds.y() / vision.height(),
                          region.bounds.width() / vision.width(),
                          region.bounds.height() / vision.height());
      textAreaFraction += bounds.width() * bounds.height();
      textPositions.append(
          QStringLiteral("%1:%2:%3:%4:%5")
              .arg(bounds.x(), 0, 'f', 5)
              .arg(bounds.y(), 0, 'f', 5)
              .arg(bounds.width(), 0, 'f', 5)
              .arg(bounds.height(), 0, 'f', 5)
              .arg(region.text.simplified()));
    }
    double fullTextAreaFraction = 0.0;
    for (const auto& region : fullText) {
      fullTextAreaFraction +=
          region.bounds.width() * region.bounds.height() / (image.width() * image.height());
    }
    const auto visionSplit = AppleVisionDetector::detectPageSplit(vision);
    const auto platePrior = PlatePrior::analyze(image);
    QVector<QRectF> textBounds;
    textBounds.reserve(text.size());
    for (const auto& region : text) textBounds.append(region.bounds);
    const auto captionEvidence =
        PlatePrior::analyzeCaptionText(textBounds, vision.size());
    const auto spreadEvidence = PlatePrior::analyzeSpreadGeometry(image);
    QString splitDecisionReason;
    const auto layout = page_split::PageLayoutEstimator::estimatePageLayout(
        page_split::AUTO_LAYOUT_TYPE, image, data.xform(), data.bwThreshold(), nullptr,
        &splitDecisionReason);
    const double angle = deskew::Task::detectAutoAngle(status, data);
    auto settings = std::make_shared<select_content::Settings>();
    const QRectF pageRect = data.xform().resultingRect();
    const QRectF content =
        select_content::ContentBoxFinder::findContentBox(status, data, pageRect, settings);
    const double splitX =
        layout.numCutters() ? layout.inscribedCutterLine(0).center().x() : -1.0;
    const double contentFraction =
        pageRect.isEmpty() ? 0.0 : content.width() * content.height() / (pageRect.width() * pageRect.height());
    const QString caption =
        QString("p%1  %2  angle=%3  content=%4%  vision=%5")
            .arg(page)
            .arg(layoutName(layout))
            .arg(angle, 0, 'f', 2)
            .arg(contentFraction * 100.0, 0, 'f', 1)
            .arg(text.size());
    QImage overlay = makeOverlay(image, layout, angle, content, caption);
    const QString overlayName = QString("page-%1.png").arg(page, 3, 10, QChar('0'));
    overlay.save(options.output + "/" + overlayName);
    overlays.push_back(overlay);
    stream << page << ',' << image.width() << ',' << image.height() << ',' << text.size() << ','
           << visionSplit.leftTextRegions << ',' << visionSplit.rightTextRegions << ','
           << (visionSplit.shouldSplit ? 1 : 0) << ',' << visionSplit.splitLineX << ','
           << visionSplit.confidence << ',' << textAreaFraction << ','
           << csvQuote(textPositions.join('|')) << ',' << fullText.size() << ','
           << fullTextAreaFraction << ',' << (captionEvidence.isCaptionScale ? 1 : 0) << ','
           << captionEvidence.peripheralRegionCount << ','
           << captionEvidence.interiorAreaFraction << ','
           << (spreadEvidence.isSpread ? 1 : 0) << ',' << spreadEvidence.aspectRatio << ','
           << (spreadEvidence.hasFullHeightGutter ? 1 : 0) << ','
           << spreadEvidence.gutterCenterFraction << ',' << spreadEvidence.gutterWidthFraction << ','
           << layoutName(layout) << ',' << splitX << ',' << splitDecisionReason << ','
           << angle << ',' << content.x() << ',' << content.y() << ',' << content.width() << ','
           << content.height() << ',' << contentFraction << ','
           << (platePrior.isPlate ? 1 : 0) << ',' << platePrior.entropy << ','
           << platePrior.tonalTileFraction << ',' << platePrior.midtoneFraction << ','
           << platePrior.reason << ',' << overlayName << '\n';
    stream.flush();
  }

  constexpr int columns = 5;
  constexpr int cellWidth = 900;
  constexpr int cellHeight = 900;
  const int rows = (int(overlays.size()) + columns - 1) / columns;
  QImage sheet(columns * cellWidth, rows * cellHeight, QImage::Format_RGB32);
  sheet.fill(Qt::white);
  QPainter painter(&sheet);
  for (int i = 0; i < int(overlays.size()); ++i) {
    painter.drawImage((i % columns) * cellWidth, (i / columns) * cellHeight, overlays[i]);
  }
  painter.end();
  sheet.save(options.output + "/contact-sheet.png");
  return overlays.size() == options.pages.size() ? 0 : 1;
}
