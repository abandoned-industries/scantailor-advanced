// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"

#include <UnitsProvider.h>

#include <QDebug>

#include <utility>

#include "DebugImagesImpl.h"
#include "Dpm.h"
#include "Filter.h"
#include "FilterData.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "PageLayoutAdapter.h"
#include "PageLayoutEstimator.h"
#include "ProjectPages.h"
#include "Settings.h"
#include "SpineDarknessFinder.h"
#include "TaskStatus.h"
#include "filters/deskew/Task.h"

namespace page_split {
using imageproc::BinaryThreshold;

class Task::UiUpdater : public FilterResult {
 public:
  UiUpdater(std::shared_ptr<Filter> filter,
            std::shared_ptr<ProjectPages> pages,
            std::unique_ptr<DebugImages> dbgImg,
            const QImage& image,
            const PageInfo& pageInfo,
            const ImageTransformation& xform,
            const OptionsWidget::UiData& uiData,
            bool batchProcessing);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

 private:
  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<ProjectPages> m_pages;
  std::unique_ptr<DebugImages> m_dbg;
  QImage m_image;
  QImage m_downscaledImage;
  PageInfo m_pageInfo;
  ImageTransformation m_xform;
  OptionsWidget::UiData m_uiData;
  bool m_batchProcessing;
};


static ProjectPages::LayoutType toPageLayoutType(const PageLayout& layout) {
  switch (layout.type()) {
    case PageLayout::SINGLE_PAGE_UNCUT:
    case PageLayout::SINGLE_PAGE_CUT:
      return ProjectPages::ONE_PAGE_LAYOUT;
    case PageLayout::TWO_PAGES:
      return ProjectPages::TWO_PAGE_LAYOUT;
  }

  assert(!"Unreachable");
  return ProjectPages::ONE_PAGE_LAYOUT;
}

Task::Task(std::shared_ptr<Filter> filter,
           std::shared_ptr<Settings> settings,
           std::shared_ptr<ProjectPages> pages,
           std::shared_ptr<deskew::Task> nextTask,
           const PageInfo& pageInfo,
           const bool batchProcessing,
           const bool debug)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_pages(std::move(pages)),
      m_nextTask(std::move(nextTask)),
      m_pageInfo(pageInfo),
      m_batchProcessing(batchProcessing) {
  if (debug) {
    m_dbg = std::make_unique<DebugImagesImpl>();
  }
}

Task::~Task() = default;

FilterResultPtr Task::process(const TaskStatus& status, const FilterData& data) {
  status.throwIfCancelled();

  Settings::Record record(m_settings->getPageRecord(m_pageInfo.imageId()));
  Dependencies deps(data.origImage().size(), data.xform().preRotation(), record.combinedLayoutType());
  qDebug() << "PageSplit Task: begin"
           << "pdfPage=" << m_pageInfo.imageId().page()
           << "subPage=" << m_pageInfo.id().subPageAsString()
           << "hasParams=" << (record.params() != nullptr);

  while (true) {
    const Params* const params = record.params();

    LayoutType newLayoutType = record.combinedLayoutType();
    AutoManualMode splitLineMode = MODE_AUTO;
    PageLayout newLayout;
    QString decisionReason = params ? params->decisionReason() : QString();

    // Force re-detection when the cached layout is an auto-detected
    // SINGLE_PAGE_UNCUT result. The original auto-detection may predate
    // detector improvements, and the user has no UI to invalidate the
    // per-image cache. Manually-pinned singles (combinedLayoutType !=
    // AUTO_LAYOUT_TYPE) and cached two-page layouts are unaffected.
    const bool staleAutoSingleUncut = params
        && record.combinedLayoutType() == AUTO_LAYOUT_TYPE
        && params->pageLayout().type() == PageLayout::SINGLE_PAGE_UNCUT;

    if (!params || !deps.compatibleWith(*params) || staleAutoSingleUncut) {
      qDebug() << "PageSplit Task: recomputing"
               << "pdfPage=" << m_pageInfo.imageId().page()
               << "subPage=" << m_pageInfo.id().subPageAsString()
               << "reason="
               << (!params ? "missing-params"
                   : staleAutoSingleUncut ? "stale-auto-single"
                   : "incompatible-deps");
      if (!params || (record.combinedLayoutType() == AUTO_LAYOUT_TYPE)) {
        SpineDarknessFinder::setLogPageTag(
            QString("[pdf=%1 sub=%2]")
                .arg(m_pageInfo.imageId().page())
                .arg(m_pageInfo.id().subPageAsString()));
        newLayout = PageLayoutEstimator::estimatePageLayout(record.combinedLayoutType(), data.grayImage(), data.xform(),
                                                            data.bwThreshold(), m_dbg.get(), &decisionReason);
        SpineDarknessFinder::setLogPageTag(QString());

        status.throwIfCancelled();
      } else {
        newLayout = PageLayoutAdapter::adaptPageLayout(params->pageLayout(), data.xform().resultingRect());
        newLayoutType = newLayout.toLayoutType();
        splitLineMode = params->splitLineMode();
      }
    } else {
      PageLayout correctedPageLayout = params->pageLayout();
      PageLayoutAdapter::correctPageLayoutType(&correctedPageLayout);
      if (correctedPageLayout.type() == params->pageLayout().type()) {
        qDebug() << "PageSplit Task: using cached params"
                 << "pdfPage=" << m_pageInfo.imageId().page()
                 << "subPage=" << m_pageInfo.id().subPageAsString();
        break;
      } else {
        newLayout = correctedPageLayout;
        newLayoutType = newLayout.toLayoutType();
        splitLineMode = params->splitLineMode();
      }
    }
    deps.setLayoutType(newLayoutType);
    const Params newParams(newLayout, deps, splitLineMode, decisionReason);

    Settings::UpdateAction update;
    update.setLayoutType(newLayoutType);
    update.setParams(newParams);

#ifndef NDEBUG
    {
      Settings::Record updatedRecord(record);
      updatedRecord.update(update);
      assert(!updatedRecord.hasLayoutTypeConflict());
      // This assert effectively verifies that PageLayoutEstimator::estimatePageLayout()
      // returned a layout with of a type consistent with the requested one.
      // If it didn't, it's a bug which will in fact cause a dead loop.
    }
#endif

    bool conflict = false;
    record = m_settings->conditionalUpdate(m_pageInfo.imageId(), update, &conflict);
    if (record.params() && (record.params()->pageLayout().numCutters() > 0)) {
      const QLineF cutter = record.params()->pageLayout().cutterLine(0);
      qDebug() << "PageSplit Task: stored params"
               << "pdfPage=" << m_pageInfo.imageId().page()
               << "subPage=" << m_pageInfo.id().subPageAsString()
               << "cutterX=" << 0.5 * (cutter.x1() + cutter.x2());
    }
    if (conflict && !record.params()) {
      // If there was a conflict, it means
      // the record was updated by another
      // thread somewhere between getPageRecord()
      // and conditionalUpdate().  If that
      // external update didn't leave page
      // parameters clear, we are just going
      // to use it's data, otherwise we need
      // to process this page again for the
      // new layout type.
      continue;
    }

    break;
  }

  OptionsWidget::UiData uiData;
  uiData.setDependencies(deps);
  const PageLayout& layout = record.params()->pageLayout();
  uiData.setLayoutTypeAutoDetected(record.combinedLayoutType() == AUTO_LAYOUT_TYPE);
  uiData.setPageLayout(layout);
  uiData.setSplitLineMode(record.params()->splitLineMode());

  m_pages->setLayoutTypeFor(m_pageInfo.imageId(), toPageLayoutType(layout));

  if (m_nextTask != nullptr) {
    ImageTransformation newXform(data.xform());
    newXform.setPreCropArea(layout.pageOutline(m_pageInfo.id().subPage()).toPolygon());
    return m_nextTask->process(status, FilterData(data, newXform));
  }
  return std::make_shared<UiUpdater>(m_filter, m_pages, std::move(m_dbg), data.origImage(), m_pageInfo, data.xform(),
                                     uiData, m_batchProcessing);
}  // Task::process

/*============================ Task::UiUpdater =========================*/

Task::UiUpdater::UiUpdater(std::shared_ptr<Filter> filter,
                           std::shared_ptr<ProjectPages> pages,
                           std::unique_ptr<DebugImages> dbgImg,
                           const QImage& image,
                           const PageInfo& pageInfo,
                           const ImageTransformation& xform,
                           const OptionsWidget::UiData& uiData,
                           const bool batchProcessing)
    : m_filter(std::move(filter)),
      m_pages(std::move(pages)),
      m_dbg(std::move(dbgImg)),
      m_image(image),
      m_downscaledImage(ImageView::createDownscaledImage(image)),
      m_pageInfo(pageInfo),
      m_xform(xform),
      m_uiData(uiData),
      m_batchProcessing(batchProcessing) {}

void Task::UiUpdater::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  ui->invalidateThumbnail(m_pageInfo.id());

  if (m_batchProcessing) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
  optWidget->postUpdateUI(m_uiData);
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto view = new ImageView(m_image, m_downscaledImage, m_xform, m_uiData.pageLayout(), m_pages, m_pageInfo.imageId(),
                            m_pageInfo.leftHalfRemoved(), m_pageInfo.rightHalfRemoved());
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, m_dbg.get());

  QObject::connect(view, SIGNAL(invalidateThumbnail(const PageInfo&)), optWidget,
                   SIGNAL(invalidateThumbnail(const PageInfo&)));
  QObject::connect(view, SIGNAL(pageLayoutSetLocally(const PageLayout&)), optWidget,
                   SLOT(pageLayoutSetExternally(const PageLayout&)));
  QObject::connect(optWidget, SIGNAL(pageLayoutSetLocally(const PageLayout&)), view,
                   SLOT(pageLayoutSetExternally(const PageLayout&)));
}  // Task::UiUpdater::updateUI
}  // namespace page_split
