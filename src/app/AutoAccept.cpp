// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AutoAccept.h"

#include <memory>
#include <set>
#include <vector>

#include "AutoManualMode.h"
#include "BatchSummaries.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "StageSequence.h"
#include "filters/deskew/Dependencies.h"
#include "filters/deskew/Filter.h"
#include "filters/deskew/Params.h"
#include "filters/deskew/Settings.h"
#include "filters/page_layout/Filter.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_split/Filter.h"
#include "filters/page_split/PageLayout.h"
#include "filters/page_split/Params.h"
#include "filters/page_split/Settings.h"
#include "filters/select_content/Filter.h"
#include "filters/select_content/Params.h"
#include "filters/select_content/Settings.h"

namespace auto_accept {

void autoAcceptPageSplit(StageSequence& stages, ProjectPages& pages_, BatchSummaries& summaries) {
  auto settings = stages.pageSplitFilter()->settings();
  if (!settings) return;

  const PageSequence pages = pages_.toPageSequence(IMAGE_VIEW);
  std::set<ImageId> seen;
  int splitCount = 0, singleCount = 0;
  std::vector<ImageId> splitIds, singleIds;

  for (const PageInfo& pi : pages) {
    const ImageId& id = pi.id().imageId();
    if (seen.count(id)) continue;
    seen.insert(id);

    page_split::Settings::Record record = settings->getPageRecord(id);
    const page_split::Params* params = record.params();
    bool isSplit = params && params->pageLayout().type() == page_split::PageLayout::TWO_PAGES;

    if (isSplit) { splitCount++; splitIds.push_back(id); }
    else         { singleCount++; singleIds.push_back(id); }
  }

  // Force minority to match majority
  if (splitCount > singleCount)
    summaries.forceTwoPageForImages(singleIds);
  else if (singleCount > splitCount)
    summaries.forceSinglePageForImages(splitIds);
}

void autoSetDeskewZero(StageSequence& stages, ProjectPages& pages_, PageView currentView) {
  auto settings = stages.deskewFilter()->settings();
  if (!settings) return;

  const PageSequence pages = pages_.toPageSequence(currentView);
  std::set<PageId> pageIds;
  for (const PageInfo& pi : pages)
    pageIds.insert(pi.id());

  deskew::Params zeroParams(0.0, deskew::Dependencies(), MODE_MANUAL);
  settings->setDegrees(pageIds, zeroParams);
}

void autoAcceptContentOutliers(StageSequence& stages, ProjectPages& pages_, PageView currentView, BatchSummaries& summaries) {
  auto settings = stages.selectContentFilter()->settings();
  if (!settings) return;

  const PageSequence pages = pages_.toPageSequence(currentView);
  std::vector<PageId> outliers;

  for (const PageInfo& pi : pages) {
    std::unique_ptr<select_content::Params> params(settings->getPageParams(pi.id()));
    if (!params) continue;
    if (!params->contentRect().isValid() || !params->pageRect().isValid()) continue;
    if (params->contentDetectionMode() == MODE_DISABLED) continue;

    double pageArea = params->pageRect().width() * params->pageRect().height();
    double contentArea = params->contentRect().width() * params->contentRect().height();
    double ratio = (pageArea > 0) ? (contentArea / pageArea) : 1.0;

    if (ratio < 0.5)
      outliers.push_back(pi.id());
  }

  if (!outliers.empty())
    summaries.preserveLayoutForPages(outliers);
}

void autoAcceptPageSizeOutliers(StageSequence& stages, BatchSummaries& summaries) {
  auto settings = stages.pageLayoutFilter()->settings();
  if (!settings) return;

  auto outliers = settings->getOutlierPages(1.3);
  if (!outliers.empty()) {
    std::vector<PageId> ids;
    for (const auto& o : outliers)
      ids.push_back(o.pageId);
    summaries.disableAlignmentForPages(ids);
  }
}

}  // namespace auto_accept
