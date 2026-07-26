// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"

#include <UnitsProvider.h>

#include <iostream>
#include <utility>

#include "ContentBoxFinder.h"
#include "DebugImagesImpl.h"
#include "Dpm.h"
#include "Filter.h"
#include "FilterData.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "PageFinder.h"
#include "TaskStatus.h"
#include "filters/page_box/Settings.h"
#include "filters/page_layout/Task.h"

using namespace imageproc;

namespace select_content {
class Task::UiUpdater : public FilterResult {
 public:
  UiUpdater(std::shared_ptr<Filter> filter,
            const PageId& pageId,
            std::unique_ptr<DebugImages> dbg,
            const QImage& image,
            const ImageTransformation& xform,
            const ContentMask& contentMask,
            const OptionsWidget::UiData& uiData,
            bool batch);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

 private:
  std::shared_ptr<Filter> m_filter;
  PageId m_pageId;
  std::unique_ptr<DebugImages> m_dbg;
  QImage m_image;
  QImage m_downscaledImage;
  ContentMask m_contentMask;
  ImageTransformation m_xform;
  OptionsWidget::UiData m_uiData;
  bool m_batchProcessing;
};


Task::Task(std::shared_ptr<Filter> filter,
           std::shared_ptr<page_layout::Task> nextTask,
           std::shared_ptr<Settings> settings,
           std::shared_ptr<page_box::Settings> pageBoxSettings,
           const PageId& pageId,
           const bool batch,
           const bool debug)
    : m_filter(std::move(filter)),
      m_nextTask(std::move(nextTask)),
      m_settings(std::move(settings)),
      m_pageBoxSettings(std::move(pageBoxSettings)),
      m_pageId(pageId),
      m_batchProcessing(batch) {
  if (debug) {
    m_dbg = std::make_unique<DebugImagesImpl>();
  }
}

Task::~Task() = default;

FilterResultPtr Task::process(const TaskStatus& status, const FilterData& data) {
  status.throwIfCancelled();

  std::unique_ptr<Params> params(m_settings->getPageParams(m_pageId));
  const Dependencies deps = (params) ? Dependencies(data.xform().resultingPreCropArea(), params->contentDetectionMode(),
                                                    params->pageDetectionMode(), params->isFineTuningEnabled())
                                     : Dependencies(data.xform().resultingPreCropArea());

  Params newParams(deps);
  if (params) {
    newParams = *params;
    newParams.setDependencies(deps);
  }

  const PhysSizeCalc physSizeCalc(data.xform());

  // Get page rect from page_box filter's settings (page box detection is now a separate stage)
  QRectF pageRect = data.xform().resultingRect();  // fallback
  if (m_pageBoxSettings) {
    auto pageBoxParams = m_pageBoxSettings->getPageParams(m_pageId);
    if (pageBoxParams && pageBoxParams->pageRect().isValid()) {
      pageRect = pageBoxParams->pageRect();
    }
  }
  newParams.setPageRect(pageRect);

  bool needUpdateContentBox = false;

  // Check if page rect changed from what was stored — if so, force content re-detection
  if (params && params->pageRect().isValid() && pageRect != params->pageRect()) {
    needUpdateContentBox = true;
  }

  if (!params || needUpdateContentBox || !deps.compatibleWith(params->dependencies(), &needUpdateContentBox, nullptr)) {
    QRectF contentRect(newParams.contentRect());

    // Force update if content box doesn't fit the (potentially new) page rect
    if (contentRect.isValid() && (contentRect.intersected(pageRect) != contentRect)) {
      needUpdateContentBox = true;
    }

    if (needUpdateContentBox) {
      if (newParams.contentDetectionMode() == MODE_AUTO) {
        QString decisionReason;
        contentRect = ContentBoxFinder::findContentBox(
            status, data, pageRect, m_settings, m_dbg.get(), &decisionReason);
        newParams.setDecisionReason(decisionReason);
      } else if (newParams.contentDetectionMode() == MODE_DISABLED) {
        contentRect = pageRect;
      }

      if (contentRect.isValid()) {
        contentRect &= pageRect;
      }

      newParams.setContentRect(contentRect);
      newParams.setContentSizeMM(physSizeCalc.sizeMM(contentRect));
    }
  }

  OptionsWidget::UiData uiData;
  uiData.setSizeCalc(physSizeCalc);
  uiData.setContentRect(newParams.contentRect());
  uiData.setPageRect(newParams.pageRect());
  uiData.setDependencies(deps);
  uiData.setContentDetectionMode(newParams.contentDetectionMode());
  uiData.setPageDetectionMode(newParams.pageDetectionMode());
  uiData.setFineTuneCornersEnabled(newParams.isFineTuningEnabled());

  m_settings->setPageParams(m_pageId, newParams);

  status.throwIfCancelled();

  if (m_nextTask) {
    return m_nextTask->process(status, FilterData(data, data.xform()), uiData.pageRect(), uiData.contentRect());
  } else {
    return std::make_shared<UiUpdater>(m_filter, m_pageId, std::move(m_dbg), data.origImage(), data.xform(),
                                       ContentMask(data.grayImageBlackOnWhite(), data.xform(), status), uiData,
                                       m_batchProcessing);
  }
}  // Task::process

/*============================ Task::UiUpdater ==========================*/

Task::UiUpdater::UiUpdater(std::shared_ptr<Filter> filter,
                           const PageId& pageId,
                           std::unique_ptr<DebugImages> dbg,
                           const QImage& image,
                           const ImageTransformation& xform,
                           const ContentMask& contentMask,
                           const OptionsWidget::UiData& uiData,
                           const bool batch)
    : m_filter(std::move(filter)),
      m_pageId(pageId),
      m_dbg(std::move(dbg)),
      m_image(image),
      m_downscaledImage(ImageView::createDownscaledImage(image)),
      m_contentMask(contentMask),
      m_xform(xform),
      m_uiData(uiData),
      m_batchProcessing(batch) {}

void Task::UiUpdater::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  ui->invalidateThumbnail(m_pageId);

  if (m_batchProcessing) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
  optWidget->postUpdateUI(m_uiData);
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  // Page rect shown as read-only context (editing is in the Page Box stage)
  auto* view = new ImageView(m_image, m_downscaledImage, m_contentMask, m_xform, m_uiData.contentRect(),
                             m_uiData.pageRect(), false);
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, m_dbg.get());

  QObject::connect(view, SIGNAL(manualContentRectSet(const QRectF&)), optWidget,
                   SLOT(manualContentRectSet(const QRectF&)));
}
}  // namespace select_content
