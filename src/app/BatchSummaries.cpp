// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BatchSummaries.h"

#include <QFileInfo>
#include <memory>
#include <set>

#include "AutoManualMode.h"
#include "BatchProcessingSummaryDialog.h"
#include "ContentCoverageSummaryDialog.h"
#include "PageBoxSummaryDialog.h"
#include "PageSequence.h"
#include "PageSizeWarningDialog.h"
#include "ProjectPages.h"
#include "StageSequence.h"
#include "ThumbnailSequence.h"
#include "filters/page_box/Filter.h"
#include "filters/page_box/Settings.h"
#include "filters/page_layout/Filter.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_split/Filter.h"
#include "filters/page_split/LayoutType.h"
#include "filters/page_split/PageLayout.h"
#include "filters/page_split/Params.h"
#include "filters/page_split/Settings.h"
#include "filters/select_content/Filter.h"
#include "filters/select_content/Params.h"
#include "filters/select_content/Settings.h"

BatchSummaries::BatchSummaries(BatchSummariesContext& context, QWidget* dialogParent)
    : m_context(context), m_dialogParent(dialogParent) {}

void BatchSummaries::showBatchProcessingSummary(const QString& timingSummary, const QString& timingBreakdown) {
  if (!m_context.stages() || !m_context.pages()) {
    return;
  }

  // Get the page_split settings
  auto pageSplitSettings = m_context.stages()->pageSplitFilter()->settings();
  if (!pageSplitSettings) {
    return;
  }

  // Get all images and count split vs single pages
  const PageSequence pages = m_context.pages()->toPageSequence(IMAGE_VIEW);
  int totalImages = 0;
  int splitPages = 0;
  int singlePages = 0;
  std::vector<BatchProcessingSummaryDialog::PageSummary> singlePageList;
  std::vector<BatchProcessingSummaryDialog::PageSummary> splitPageList;

  // Track which ImageIds we've seen to avoid counting the same image twice
  std::set<ImageId> seenImages;

  for (const PageInfo& pageInfo : pages) {
    const ImageId& imageId = pageInfo.id().imageId();

    // Skip if we've already processed this image
    if (seenImages.find(imageId) != seenImages.end()) {
      continue;
    }
    seenImages.insert(imageId);
    totalImages++;

    // Get the page split record for this image
    page_split::Settings::Record record = pageSplitSettings->getPageRecord(imageId);
    const page_split::Params* params = record.params();

    bool isSplit = false;
    if (params) {
      // Check the actual PageLayout type
      const page_split::PageLayout& layout = params->pageLayout();
      isSplit = (layout.type() == page_split::PageLayout::TWO_PAGES);
    }

    BatchProcessingSummaryDialog::PageSummary summary;
    summary.imageId = imageId;
    summary.fileName = QFileInfo(imageId.filePath()).fileName();
    summary.pageNumber = totalImages;
    summary.isSplit = isSplit;

    if (isSplit) {
      splitPages++;
      splitPageList.push_back(summary);
    } else {
      singlePages++;
      singlePageList.push_back(summary);
    }
  }

  // Create and show the dialog
  auto* dialog = new BatchProcessingSummaryDialog(m_dialogParent);
  dialog->setSummary(totalImages, splitPages, singlePages, singlePageList, splitPageList);
  dialog->setTimingDetails(timingSummary, timingBreakdown);

  connect(dialog, &BatchProcessingSummaryDialog::jumpToPage,
          this, &BatchSummaries::jumpToPageFromSummary);
  connect(dialog, &BatchProcessingSummaryDialog::forceTwoPageSelected,
          this, &BatchSummaries::forceTwoPageForImages);
  connect(dialog, &BatchProcessingSummaryDialog::forceTwoPageAll,
          this, &BatchSummaries::forceTwoPageForImages);
  connect(dialog, &BatchProcessingSummaryDialog::forceSinglePageSelected,
          this, &BatchSummaries::forceSinglePageForImages);
  connect(dialog, &BatchProcessingSummaryDialog::forceSinglePageAll,
          this, &BatchSummaries::forceSinglePageForImages);

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void BatchSummaries::jumpToPageFromSummary(const ImageId& imageId) {
  if (!m_context.thumbSequence()) {
    return;
  }

  // Find the PageId for this image and jump to it
  const PageSequence pages = m_context.pages()->toPageSequence(m_context.currentView());
  for (const PageInfo& pageInfo : pages) {
    if (pageInfo.id().imageId() == imageId) {
      m_context.jumpToPage(pageInfo.id());
      return;
    }
  }
}

void BatchSummaries::forceTwoPageForImages(const std::vector<ImageId>& imageIds) {
  if (imageIds.empty() || !m_context.stages()) {
    return;
  }

  // Build a set of PageIds from the image IDs
  std::set<PageId> pageIds;
  const PageSequence pages = m_context.pages()->toPageSequence(IMAGE_VIEW);
  for (const PageInfo& pageInfo : pages) {
    for (const ImageId& imageId : imageIds) {
      if (pageInfo.id().imageId() == imageId) {
        pageIds.insert(pageInfo.id());
      }
    }
  }

  if (pageIds.empty()) {
    return;
  }

  // Set layout type to TWO_PAGES for all the selected pages
  m_context.stages()->pageSplitFilter()->settings()->setLayoutTypeFor(
      page_split::TWO_PAGES, pageIds);

  // Invalidate thumbnails for these pages to trigger re-processing
  for (const PageId& pageId : pageIds) {
    m_context.thumbSequence()->invalidateThumbnail(pageId);
  }

  // Refresh the current view if we're on Page Split filter
  if (m_context.currentFilterIndex() == m_context.stages()->pageSplitFilterIdx()) {
    m_context.updateMainArea();
  }
}

void BatchSummaries::forceSinglePageForImages(const std::vector<ImageId>& imageIds) {
  if (imageIds.empty() || !m_context.stages()) {
    return;
  }

  // Build a set of PageIds from the image IDs
  std::set<PageId> pageIds;
  const PageSequence pages = m_context.pages()->toPageSequence(IMAGE_VIEW);
  for (const PageInfo& pageInfo : pages) {
    for (const ImageId& imageId : imageIds) {
      if (pageInfo.id().imageId() == imageId) {
        pageIds.insert(pageInfo.id());
      }
    }
  }

  if (pageIds.empty()) {
    return;
  }

  // Set layout type to SINGLE_PAGE_UNCUT for all the selected pages
  m_context.stages()->pageSplitFilter()->settings()->setLayoutTypeFor(
      page_split::SINGLE_PAGE_UNCUT, pageIds);

  // Invalidate thumbnails for these pages to trigger re-processing
  for (const PageId& pageId : pageIds) {
    m_context.thumbSequence()->invalidateThumbnail(pageId);
  }

  // Refresh the current view if we're on Page Split filter
  if (m_context.currentFilterIndex() == m_context.stages()->pageSplitFilterIdx()) {
    m_context.updateMainArea();
  }
}

void BatchSummaries::showPageBoxSummary() {
  if (!m_context.stages() || !m_context.pages()) {
    return;
  }

  auto pageBoxSettings = m_context.stages()->pageBoxFilter()->settings();
  if (!pageBoxSettings) {
    return;
  }

  const PageSequence pages = m_context.pages()->toPageSequence(m_context.currentView());
  std::vector<PageBoxSummaryDialog::PageSummary> allPages;
  int pageNumber = 0;

  for (const PageInfo& pageInfo : pages) {
    const PageId& pageId = pageInfo.id();
    pageNumber++;

    auto params = pageBoxSettings->getPageParams(pageId);
    if (!params) {
      continue;
    }

    const QRectF& pageRect = params->pageRect();
    if (!pageRect.isValid()) {
      continue;
    }

    PageBoxSummaryDialog::PageSummary summary;
    summary.pageId = pageId;
    summary.fileName = QFileInfo(pageId.imageId().filePath()).fileName();
    summary.pageNumber = pageNumber;
    summary.pageWidth = pageRect.width();
    summary.deviationPercent = 0;  // computed by dialog from median
    allPages.push_back(summary);
  }

  if (allPages.empty()) {
    return;
  }

  auto* dialog = new PageBoxSummaryDialog(m_dialogParent);
  dialog->setSummary(static_cast<int>(allPages.size()), allPages, 10);

  connect(dialog, &PageBoxSummaryDialog::jumpToPage,
          this, [this](const PageId& pageId) { m_context.jumpToPage(pageId); });
  connect(dialog, &PageBoxSummaryDialog::disablePageBoxSelected,
          this, [this](const std::vector<PageId>& pageIds) {
            auto settings = m_context.stages()->pageBoxFilter()->settings();
            for (const PageId& pid : pageIds) {
              auto params = settings->getPageParams(pid);
              if (params) {
                params->setPageDetectionMode(MODE_DISABLED);
                settings->setPageParams(pid, *params);
              }
            }
            m_context.thumbSequence()->invalidateAllThumbnails();
          });
  connect(dialog, &PageBoxSummaryDialog::disablePageBoxAll,
          this, [this](const std::vector<PageId>& pageIds) {
            auto settings = m_context.stages()->pageBoxFilter()->settings();
            for (const PageId& pid : pageIds) {
              auto params = settings->getPageParams(pid);
              if (params) {
                params->setPageDetectionMode(MODE_DISABLED);
                settings->setPageParams(pid, *params);
              }
            }
            m_context.thumbSequence()->invalidateAllThumbnails();
          });

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void BatchSummaries::showContentCoverageSummary() {
  if (!m_context.stages() || !m_context.pages()) {
    return;
  }

  // Get the select_content settings
  auto selectContentSettings = m_context.stages()->selectContentFilter()->settings();
  if (!selectContentSettings) {
    return;
  }

  // Get all pages and calculate coverage ratios
  const PageSequence pages = m_context.pages()->toPageSequence(m_context.currentView());
  std::vector<ContentCoverageSummaryDialog::PageSummary> allPages;
  int pageNumber = 0;

  for (const PageInfo& pageInfo : pages) {
    const PageId& pageId = pageInfo.id();
    pageNumber++;

    std::unique_ptr<select_content::Params> params(selectContentSettings->getPageParams(pageId));
    if (!params) {
      continue;
    }

    const QRectF& contentRect = params->contentRect();
    const QRectF& pageRect = params->pageRect();

    // Skip pages with invalid rects
    if (!contentRect.isValid() || !pageRect.isValid()) {
      continue;
    }

    // Skip pages where content detection is disabled (already preserved layout)
    if (params->contentDetectionMode() == MODE_DISABLED) {
      continue;
    }

    double pageArea = pageRect.width() * pageRect.height();
    double contentArea = contentRect.width() * contentRect.height();
    double coverageRatio = (pageArea > 0) ? (contentArea / pageArea) : 1.0;

    ContentCoverageSummaryDialog::PageSummary summary;
    summary.pageId = pageId;
    summary.fileName = QFileInfo(pageId.imageId().filePath()).fileName();
    summary.pageNumber = pageNumber;
    summary.coverageRatio = coverageRatio;
    allPages.push_back(summary);
  }

  // Only show dialog if there are pages to display
  if (allPages.empty()) {
    return;
  }

  // Create and show the dialog
  auto* dialog = new ContentCoverageSummaryDialog(m_dialogParent);
  dialog->setSummary(static_cast<int>(allPages.size()), allPages, 0.5);

  connect(dialog, &ContentCoverageSummaryDialog::jumpToPage,
          this, &BatchSummaries::jumpToPageFromContentSummary);
  connect(dialog, &ContentCoverageSummaryDialog::preserveLayoutSelected,
          this, &BatchSummaries::preserveLayoutForPages);
  connect(dialog, &ContentCoverageSummaryDialog::preserveLayoutAll,
          this, &BatchSummaries::preserveLayoutForPages);

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void BatchSummaries::jumpToPageFromContentSummary(const PageId& pageId) {
  if (!m_context.thumbSequence()) {
    return;
  }

  m_context.jumpToPage(pageId);
}

void BatchSummaries::preserveLayoutForPages(const std::vector<PageId>& pageIds) {
  if (pageIds.empty() || !m_context.stages()) {
    return;
  }

  auto selectContentSettings = m_context.stages()->selectContentFilter()->settings();
  if (!selectContentSettings) {
    return;
  }

  // For each page, set content detection mode to DISABLED and set content rect to page rect
  for (const PageId& pageId : pageIds) {
    std::unique_ptr<select_content::Params> params(selectContentSettings->getPageParams(pageId));
    if (!params) {
      continue;
    }

    // Set content detection mode to DISABLED (preserves original page layout)
    params->setContentDetectionMode(MODE_DISABLED);

    // Set content rect to match page rect (full page)
    params->setContentRect(params->pageRect());

    // Save the updated params
    selectContentSettings->setPageParams(pageId, *params);

    // Invalidate thumbnail to show the change
    if (m_context.thumbSequence()) {
      m_context.thumbSequence()->invalidateThumbnail(pageId);
    }
  }

  // Refresh the current view if we're on Select Content filter
  if (m_context.currentFilterIndex() == m_context.stages()->selectContentFilterIdx()) {
    m_context.updateMainArea();
  }
}

void BatchSummaries::showPageSizeWarning() {
  if (!m_context.stages() || !m_context.pages()) {
    return;
  }

  // Get the page_layout settings
  auto pageLayoutSettings = m_context.stages()->pageLayoutFilter()->settings();
  if (!pageLayoutSettings) {
    return;
  }

  // Get the aggregate size first (needed for spread detection)
  QSizeF aggSize = pageLayoutSettings->getAggregateHardSizeMM();

  // Check if we have valid aggregate size - if not, data hasn't been populated yet
  if (!aggSize.isValid() || aggSize.isEmpty()) {
    return;
  }

  // Get outlier pages (default threshold 1.3 = 30% deviation)
  auto outlierPages = pageLayoutSettings->getOutlierPages(1.3);

  // Get median size - if no outliers, we still need to check for spreads
  double medianWidthMM = 0;
  double medianHeightMM = 0;

  if (!outlierPages.empty()) {
    medianWidthMM = outlierPages[0].medianWidthMM;
    medianHeightMM = outlierPages[0].medianHeightMM;
  } else {
    // No outliers - try to get median some other way or use aggregate
    // For now, estimate based on typical page ratio vs aggregate
    // If aggregate width is ~2x a typical portrait page, it's likely spreads
    double aspectRatio = aggSize.height() > 0 ? aggSize.width() / aggSize.height() : 1.0;
    if (aspectRatio > 1.3) {  // Landscape/spread-like aspect ratio
      medianWidthMM = aggSize.width() / 2.0;  // Estimate half width as typical
      medianHeightMM = aggSize.height();
    } else {
      // Can't determine - don't show dialog
      return;
    }
  }

  // Check if this looks like a spread situation (aggregate ~2x median width)
  double widthRatio = (medianWidthMM > 0) ? (aggSize.width() / medianWidthMM) : 1.0;
  bool likelySpreads = (widthRatio > 1.8 && widthRatio < 2.2);

  // Only show dialog if there are outlier pages OR it looks like spreads
  if (outlierPages.empty() && !likelySpreads) {
    return;
  }

  // Get all pages to count them and assign page numbers
  const PageSequence pages = m_context.pages()->toPageSequence(m_context.currentView());

  // Convert to dialog's OutlierInfo format
  // Match by ImageId rather than full PageId to handle sub-page differences
  std::vector<PageSizeWarningDialog::OutlierInfo> dialogOutliers;
  for (const auto& outlier : outlierPages) {
    // Find this outlier's page number in the sequence
    int pageNumber = 0;
    bool found = false;
    for (const PageInfo& pageInfo : pages) {
      pageNumber++;
      // Match by ImageId to be more flexible with sub-page differences
      if (pageInfo.id().imageId() == outlier.pageId.imageId()) {
        found = true;
        break;
      }
    }

    if (found) {
      PageSizeWarningDialog::OutlierInfo info;
      info.pageId = outlier.pageId;
      info.fileName = QFileInfo(outlier.pageId.imageId().filePath()).fileName();
      info.pageNumber = pageNumber;
      info.hardWidthMM = outlier.hardWidthMM;
      info.hardHeightMM = outlier.hardHeightMM;
      info.medianWidthMM = outlier.medianWidthMM;
      info.medianHeightMM = outlier.medianHeightMM;
      info.deviationRatio = outlier.deviationRatio;
      info.isLarger = outlier.isLarger;
      info.setsAggregateWidth = outlier.setsAggregateWidth;
      info.setsAggregateHeight = outlier.setsAggregateHeight;
      dialogOutliers.push_back(info);
    }
  }

  // Create and show the dialog
  auto* dialog = new PageSizeWarningDialog(m_dialogParent);

  // When likelySpreads is true, ALWAYS use spread mode - it's more accurate than area-based outlier detection
  if (likelySpreads) {
    // Spread mode - get only unsplit spread pages (not all pages)
    auto unsplitSpreads = pageLayoutSettings->getUnsplitSpreadPages();

    if (unsplitSpreads.empty()) {
      // No unsplit spreads found, don't show dialog
      delete dialog;
      return;
    }

    // Convert to dialog format and assign page numbers
    // For spread pages, we need to:
    // 1. Deduplicate by ImageId (since Settings may have LEFT_PAGE and RIGHT_PAGE entries for same image)
    // 2. Use IMAGE_VIEW to get correct image numbers (not sub-page numbers)

    // Get image-based page sequence for correct numbering
    const PageSequence imagePages = m_context.pages()->toPageSequence(IMAGE_VIEW);

    // Track which ImageIds we've already added to avoid duplicates
    std::set<ImageId> seenImageIds;
    std::vector<PageSizeWarningDialog::OutlierInfo> spreadPages;

    for (const auto& spread : unsplitSpreads) {
      // Skip if we've already processed this image
      if (seenImageIds.count(spread.pageId.imageId())) {
        continue;
      }
      seenImageIds.insert(spread.pageId.imageId());

      // Find this spread's image number in the sequence
      int imageNum = 0;
      bool found = false;
      for (const PageInfo& pageInfo : imagePages) {
        imageNum++;
        if (pageInfo.id().imageId() == spread.pageId.imageId()) {
          found = true;
          break;
        }
      }

      if (found) {
        PageSizeWarningDialog::OutlierInfo info;
        info.pageId = spread.pageId;
        info.fileName = QFileInfo(spread.pageId.imageId().filePath()).fileName();
        info.pageNumber = imageNum;  // Use image number, not sub-page number
        info.hardWidthMM = spread.hardWidthMM;
        info.hardHeightMM = spread.hardHeightMM;
        info.medianWidthMM = spread.medianWidthMM;
        info.medianHeightMM = spread.medianHeightMM;
        info.deviationRatio = spread.deviationRatio;
        info.isLarger = spread.isLarger;
        info.setsAggregateWidth = spread.setsAggregateWidth;
        info.setsAggregateHeight = spread.setsAggregateHeight;
        spreadPages.push_back(info);
      }
    }

    if (spreadPages.empty()) {
      // No unsplit spreads matched to page sequence - don't show dialog
      delete dialog;
      return;
    }

    dialog->setSpreadPages(pages.numPages(),
                            medianWidthMM, medianHeightMM,
                            aggSize.width(), aggSize.height(),
                            spreadPages);

    connect(dialog, &PageSizeWarningDialog::goToPageSplitStage,
            this, &BatchSummaries::goToPageSplitFromWarning);
  } else {
    // Outlier mode
    dialog->setOutlierPages(pages.numPages(),
                            medianWidthMM, medianHeightMM,
                            aggSize.width(), aggSize.height(),
                            dialogOutliers, 1.3);
  }

  connect(dialog, &PageSizeWarningDialog::jumpToPage,
          this, &BatchSummaries::jumpToPageFromPageSizeWarning);
  connect(dialog, &PageSizeWarningDialog::detachPagesFromSizing,
          this, &BatchSummaries::disableAlignmentForPages);

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void BatchSummaries::jumpToPageFromPageSizeWarning(const PageId& pageId) {
  if (!m_context.thumbSequence() || !m_context.pages()) {
    return;
  }

  // Find the actual PageId in the current view that matches this ImageId
  // (the stored PageId may have different sub-page info)
  const PageSequence pages = m_context.pages()->toPageSequence(m_context.currentView());
  for (const PageInfo& pageInfo : pages) {
    if (pageInfo.id().imageId() == pageId.imageId()) {
      m_context.jumpToPage(pageInfo.id());
      return;
    }
  }

  // Fallback to original pageId if no match found
  m_context.jumpToPage(pageId);
}

void BatchSummaries::goToPageSplitFromWarning() {
  if (!m_context.stages()) {
    return;
  }

  // Switch to the Page Split filter (stage 2)
  m_context.selectFilterListRow(m_context.stages()->pageSplitFilterIdx());
}

void BatchSummaries::disableAlignmentForPages(const std::vector<PageId>& pageIds) {
  if (pageIds.empty() || !m_context.stages()) {
    return;
  }

  auto pageLayoutSettings = m_context.stages()->pageLayoutFilter()->settings();
  if (!pageLayoutSettings) {
    return;
  }

  // Disable alignment for all specified pages
  pageLayoutSettings->disableAlignmentForPages(pageIds);

  // Invalidate all thumbnails since aggregate size might change
  if (m_context.thumbSequence()) {
    m_context.thumbSequence()->invalidateAllThumbnails();
  }

  // Refresh the current view if we're on Margins filter
  if (m_context.currentFilterIndex() == m_context.stages()->pageLayoutFilterIdx()) {
    m_context.updateMainArea();
  }
}
