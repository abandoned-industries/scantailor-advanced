#include <BinaryThreshold.h>
#include <FilterData.h>
#include <ImageId.h>
#include <ImageSettings.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <PdfReader.h>
#include <TiffWriter.h>
#include <WhiteBalance.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPolygonF>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include "filters/output/DepthPerception.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"
#include "filters/page_layout/Alignment.h"
#include "filters/page_layout/Params.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_layout/Utils.h"

namespace {

struct Page {
  int pdfIndex;
  PageId id;
  FilterData data;
  QRectF pageRect;
  QRectF contentRect;
};

QRect deriveContentRect(const QImage& image) {
  const QColor background = WhiteBalance::estimateBackgroundColor(image);
  const int backgroundLuma = background.isValid()
                                 ? (background.red() + background.green()
                                    + background.blue()) / 3
                                 : 220;
  const int inkThreshold = std::clamp(backgroundLuma - 35, 70, 190);
  const int guardX = std::max(1, image.width() * 3 / 100);
  const int guardY = std::max(1, image.height() * 2 / 100);
  const int step = std::max(
      1, std::min(image.width(), image.height()) / 1200);
  const QImage rgb(image.convertToFormat(QImage::Format_RGB32));
  int left = image.width();
  int top = image.height();
  int right = -1;
  int bottom = -1;
  for (int y = guardY; y < image.height() - guardY; y += step) {
    const auto* line =
        reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
    for (int x = guardX; x < image.width() - guardX; x += step) {
      const int luma =
          (qRed(line[x]) + qGreen(line[x]) + qBlue(line[x])) / 3;
      if (luma < inkThreshold) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
      }
    }
  }
  if (right < left || bottom < top) {
    return image.rect().adjusted(
        image.width() / 8, image.height() / 8,
        -image.width() / 8, -image.height() / 8);
  }
  return QRect(
             QPoint(left - image.width() * 2 / 100,
                    top - image.height() * 2 / 100),
             QPoint(right + image.width() * 2 / 100,
                    bottom + image.height() * 2 / 100))
      .intersected(image.rect());
}

QImage render(const Page& page,
              const page_layout::Settings& layoutSettings,
              bool fullBleed,
              QRect* outputContentRect) {
  std::unique_ptr<page_layout::Params> layoutParams(
      layoutSettings.getPageParams(page.id));
  if (!layoutParams) {
    return QImage();
  }
  if (layoutParams->isFullBleed() != fullBleed) {
    layoutParams = std::make_unique<page_layout::Params>(
        layoutParams->hardMarginsMM(), layoutParams->pageRect(),
        layoutParams->contentRect(), layoutParams->contentSizeMM(),
        layoutParams->alignment(), layoutParams->isAutoMarginsEnabled(),
        fullBleed);
  }
  const QRectF effectiveContent(
      fullBleed ? page.pageRect : page.contentRect);
  const QRectF adapted(page_layout::Utils::adaptContentRect(
      page.data.xform(), effectiveContent));
  const QPolygonF contentPhys(
      page.data.xform().transformBack().map(adapted));
  const QPolygonF pagePhys(page_layout::Utils::calcPageRectPhys(
      page.data.xform(), contentPhys, *layoutParams,
      layoutSettings.getAggregateHardSizeMM()));
  ImageTransformation xform(page.data.xform());
  xform.setPostCropArea(page_layout::Utils::shiftToRoundedOrigin(
      xform.transform().map(pagePhys)));
  xform.postScaleToDpi(Dpi(300, 300));

  auto outputSettings = std::make_shared<output::Settings>();
  output::Params params;
  params.setOutputDpi(Dpi(300, 300));
  params.setDespeckleLevel(0.0);
  output::ColorParams colorParams(params.colorParams());
  colorParams.setColorMode(output::BLACK_AND_WHITE);
  params.setColorParams(colorParams);
  outputSettings->setParams(page.id, params);

  output::OutputGenerator generator(xform, contentPhys);
  ZoneSet pictureZones;
  const ZoneSet fillZones;
  dewarping::DistortionModel distortionModel;
  NullTaskStatus status;
  std::unique_ptr<output::OutputImage> result(generator.process(
      status, FilterData(page.data, xform), pictureZones, fillZones,
      distortionModel, output::DepthPerception(), nullptr, nullptr, nullptr,
      page.id, outputSettings));
  if (!result) {
    return QImage();
  }
  *outputContentRect = generator.outputContentRect();
  return result->toImage();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList args(app.arguments());
  if (args.size() != 10) {
    std::fprintf(
        stderr,
        "usage: %s INPUT_PDF OUTPUT_DIR SCENARIO PAGE_INDEX... (6 pages)\n",
        argv[0]);
    return 2;
  }
  const QString input(args[1]);
  const QString outputDirPath(args[2]);
  const QString scenario(args[3]);
  if (scenario != QStringLiteral("defaults")
      && scenario != QStringLiteral("mixed")) {
    return 2;
  }
  QDir().mkpath(outputDirPath);
  QDir outputDir(outputDirPath);

  std::vector<Page> pages;
  for (int i = 4; i < args.size(); ++i) {
    bool ok = false;
    const int pdfIndex = args[i].toInt(&ok);
    if (!ok || pdfIndex < 0) {
      return 2;
    }
    QImage image(PdfReader::readImage(input, pdfIndex, 300));
    if (image.isNull()) {
      return 1;
    }
    FilterData data(image);
    data.updateImageParams(
        ImageSettings::PageParams(imageproc::BinaryThreshold(128), true));
    QRectF contentRect(deriveContentRect(image));
    if (scenario == QStringLiteral("mixed")) {
      const int ordinal = i - 4;
      const double inset = 0.015 * ordinal;
      contentRect.adjust(
          image.width() * inset, image.height() * inset * 0.7,
          -image.width() * inset * 0.55,
          -image.height() * inset);
    }
    const PageId pageId(ImageId(QStringLiteral("text fixture"), pdfIndex + 1));
    pages.push_back(
        {pdfIndex, pageId, data, data.xform().resultingRect(), contentRect});
  }

  page_layout::Settings layoutSettings;
  const Margins margins(10.0, 5.0, 10.0, 5.0);
  const page_layout::Alignment alignment(
      page_layout::Alignment::TOP, page_layout::Alignment::HCENTER);
  for (const Page& page : pages) {
    layoutSettings.setPageParams(
        page.id,
        page_layout::Params(
            margins, page.pageRect, page.contentRect,
            page_layout::Utils::calcRectSizeMM(
                page.data.xform(), page.contentRect),
            alignment, false, false));
  }

  QFile csv(outputDir.filePath(QStringLiteral("dimensions.csv")));
  if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return 1;
  }
  QTextStream stream(&csv);
  stream << "page,route,width,height,content_x,content_y,content_width,"
            "content_height\n";
  for (const Page& page : pages) {
    for (bool fullBleed : {true, false}) {
      QRect contentRect;
      const QImage image(
          render(page, layoutSettings, fullBleed, &contentRect));
      if (image.isNull()) {
        return 1;
      }
      const QString route(
          fullBleed ? QStringLiteral("shortcut-full-bleed")
                    : QStringLiteral("standardized"));
      const QString fileName(
          QStringLiteral("page-%1-%2.tif")
              .arg(page.pdfIndex + 1, 3, 10, QLatin1Char('0'))
              .arg(route));
      if (!TiffWriter::writeImage(outputDir.filePath(fileName), image)) {
        return 1;
      }
      stream << page.pdfIndex + 1 << ',' << route << ',' << image.width()
             << ',' << image.height() << ',' << contentRect.x() << ','
             << contentRect.y() << ',' << contentRect.width() << ','
             << contentRect.height() << '\n';
    }
  }
  return 0;
}
