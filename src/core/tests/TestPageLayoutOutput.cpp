#include <BinaryThreshold.h>
#include <FilterData.h>
#include <ImageId.h>
#include <ImageSettings.h>
#include <NullTaskStatus.h>
#include <PageId.h>

#include <QImage>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>

#include "filters/output/DepthPerception.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"
#include "filters/page_layout/Alignment.h"
#include "filters/page_layout/Params.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_layout/Utils.h"

namespace {

struct RenderedPage {
  QImage image;
  QRect contentRect;
};

FilterData makeData(const QSize& size) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(Qt::white);
  image.setDotsPerMeterX(11811);
  image.setDotsPerMeterY(11811);
  FilterData data(image);
  data.updateImageParams(
      ImageSettings::PageParams(imageproc::BinaryThreshold(128), true));
  return data;
}

RenderedPage renderPage(const FilterData& data,
                        const PageId& pageId,
                        const QRectF& contentRect,
                        const page_layout::Settings& layoutSettings) {
  const std::unique_ptr<page_layout::Params> layoutParams(
      layoutSettings.getPageParams(pageId));
  BOOST_REQUIRE(layoutParams);

  const QRectF adaptedContentRect(
      page_layout::Utils::adaptContentRect(data.xform(), contentRect));
  const QPolygonF contentRectPhys(
      data.xform().transformBack().map(adaptedContentRect));
  const QPolygonF pageRectPhys(page_layout::Utils::calcPageRectPhys(
      data.xform(), contentRectPhys, *layoutParams,
      layoutSettings.getAggregateHardSizeMM()));

  ImageTransformation outputXform(data.xform());
  outputXform.setPostCropArea(page_layout::Utils::shiftToRoundedOrigin(
      outputXform.transform().map(pageRectPhys)));
  outputXform.postScaleToDpi(Dpi(300, 300));

  auto outputSettings = std::make_shared<output::Settings>();
  output::Params outputParams;
  outputParams.setOutputDpi(Dpi(300, 300));
  outputParams.setDespeckleLevel(0.0);
  output::ColorParams colorParams(outputParams.colorParams());
  colorParams.setColorMode(output::BLACK_AND_WHITE);
  outputParams.setColorParams(colorParams);
  outputSettings->setParams(pageId, outputParams);

  const output::OutputGenerator generator(outputXform, contentRectPhys);
  ZoneSet pictureZones;
  const ZoneSet fillZones;
  dewarping::DistortionModel distortionModel;
  NullTaskStatus status;
  std::unique_ptr<output::OutputImage> rendered(generator.process(
      status, FilterData(data, outputXform), pictureZones, fillZones,
      distortionModel, output::DepthPerception(), nullptr, nullptr, nullptr,
      pageId, outputSettings));
  BOOST_REQUIRE(rendered);
  return {rendered->toImage(), generator.outputContentRect()};
}

}  // namespace

BOOST_AUTO_TEST_SUITE(PageLayoutOutputTestSuite)

BOOST_AUTO_TEST_CASE(match_size_renders_equal_margin_inclusive_canvases) {
  const FilterData firstData(makeData(QSize(1000, 1400)));
  const FilterData secondData(makeData(QSize(920, 1320)));
  const PageId firstId(ImageId(QStringLiteral("first"), 1));
  const PageId secondId(ImageId(QStringLiteral("second"), 1));
  const QRectF firstPage(firstData.xform().resultingRect());
  const QRectF secondPage(secondData.xform().resultingRect());
  const QRectF firstContent(120, 150, 710, 1010);
  const QRectF secondContent(90, 170, 680, 940);
  const Margins hardMargins(10.0, 12.0, 14.0, 16.0);
  const page_layout::Alignment alignment(
      page_layout::Alignment::TOP, page_layout::Alignment::LEFT);

  page_layout::Settings layoutSettings;
  layoutSettings.setPageParams(
      firstId,
      page_layout::Params(
          hardMargins, firstPage, firstContent,
          page_layout::Utils::calcRectSizeMM(firstData.xform(), firstContent),
          alignment, false, false));
  layoutSettings.setPageParams(
      secondId,
      page_layout::Params(
          hardMargins, secondPage, secondContent,
          page_layout::Utils::calcRectSizeMM(secondData.xform(), secondContent),
          alignment, false, false));

  const RenderedPage first(renderPage(
      firstData, firstId, firstContent, layoutSettings));
  const RenderedPage second(renderPage(
      secondData, secondId, secondContent, layoutSettings));

  BOOST_CHECK(first.image.size() == second.image.size());
  BOOST_CHECK(first.image.width() > first.contentRect.width());
  BOOST_CHECK(first.image.height() > first.contentRect.height());
  BOOST_CHECK(second.image.width() > second.contentRect.width());
  BOOST_CHECK(second.image.height() > second.contentRect.height());

  // TOP / LEFT keeps the hard top and left margins fixed while aggregate
  // match-size space is added to the bottom and right.
  BOOST_CHECK_SMALL(first.contentRect.left() - second.contentRect.left(), 1);
  BOOST_CHECK_SMALL(first.contentRect.top() - second.contentRect.top(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
