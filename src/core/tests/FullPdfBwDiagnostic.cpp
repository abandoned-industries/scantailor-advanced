#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <BlackOnWhiteEstimator.h>
#include <FilterData.h>
#include <ImageId.h>
#include <ImageSettings.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <PdfReader.h>

#include <QCoreApplication>
#include <QImage>
#include <QPolygonF>

#include <cstdio>
#include <memory>

#include "filters/output/BlackWhiteOptions.h"
#include "filters/output/DepthPerception.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: %s INPUT_PDF [PAGE]\n", argv[0]);
    return 2;
  }

  const QString inputPath(QString::fromUtf8(argv[1]));
  const PdfReader::PdfInfo info(PdfReader::readPdfInfo(inputPath));
  if (info.pageCount <= 0) {
    return 1;
  }

  int firstIndex = 0;
  int endIndex = info.pageCount;
  if (argc == 3) {
    bool ok = false;
    const int page = QString::fromUtf8(argv[2]).toInt(&ok);
    if (!ok || page < 1 || page > info.pageCount) {
      return 2;
    }
    firstIndex = page - 1;
    endIndex = page;
  }

  NullTaskStatus status;
  std::printf("page,black_on_white,width,height,black_share\n");
  for (int pdfIndex = firstIndex; pdfIndex < endIndex; ++pdfIndex) {
    const QImage source(PdfReader::readImage(inputPath, pdfIndex, 300));
    if (source.isNull()) {
      return 1;
    }

    FilterData data(source);
    const bool blackOnWhite = BlackOnWhiteEstimator::isBlackOnWhite(
        data.grayImage(), data.xform(), status, nullptr);
    data.updateImageParams(ImageSettings::PageParams(
        imageproc::BinaryThreshold(128), blackOnWhite));
    const PageId pageId(ImageId(inputPath, pdfIndex + 1));

    ImageTransformation outputXform(data.xform());
    outputXform.setPostCropArea(outputXform.resultingRect());
    outputXform.postScaleToDpi(Dpi(300, 300));
    const QPolygonF fullPdfCanvasPhys(
        data.xform().transformBack().map(QRectF(source.rect())));

    auto settings = std::make_shared<output::Settings>();
    output::Params params;
    params.setOutputDpi(Dpi(300, 300));
    params.setBlackOnWhite(blackOnWhite);
    output::ColorParams colorParams(params.colorParams());
    colorParams.setColorMode(output::BLACK_AND_WHITE);
    output::BlackWhiteOptions bwOptions(colorParams.blackWhiteOptions());
    bwOptions.setBinarizationMethod(output::T_OTSU);
    colorParams.setBlackWhiteOptions(bwOptions);
    params.setColorParams(colorParams);
    settings->setParams(pageId, params);

    output::OutputGenerator generator(outputXform, fullPdfCanvasPhys);
    ZoneSet pictureZones;
    const ZoneSet fillZones;
    dewarping::DistortionModel distortionModel;
    std::unique_ptr<output::OutputImage> rendered(generator.process(
        status, FilterData(data, outputXform), pictureZones, fillZones,
        distortionModel, output::DepthPerception(), nullptr, nullptr, nullptr,
        pageId, settings));
    if (!rendered) {
      return 1;
    }

    const QImage result(rendered->toImage());
    const imageproc::BinaryImage binary(result, imageproc::BinaryThreshold(128));
    const double blackShare = binary.countBlackPixels()
                              / static_cast<double>(result.width())
                              / static_cast<double>(result.height());
    std::printf("%d,%d,%d,%d,%.8f\n", pdfIndex + 1,
                blackOnWhite ? 1 : 0, result.width(), result.height(), blackShare);
    std::fflush(stdout);
  }
  return 0;
}
