// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_BATCHSUMMARIES_H_
#define SCANTAILOR_APP_BATCHSUMMARIES_H_

#include <QObject>
#include <QString>
#include <vector>

#include "PageView.h"

class ImageId;
class PageId;
class ProjectPages;
class StageSequence;
class ThumbnailSequence;
class QWidget;

// Narrow window-side surface the batch summary/remediation dialog suite needs.
// Implemented by MainWindow; accessors are resolved at call time so the suite
// always sees the current project's state.
class BatchSummariesContext {
 public:
  virtual ~BatchSummariesContext() = default;

  virtual StageSequence* stages() const = 0;
  virtual ProjectPages* pages() const = 0;
  virtual ThumbnailSequence* thumbSequence() const = 0;
  virtual PageView currentView() const = 0;
  virtual int currentFilterIndex() const = 0;
  virtual void jumpToPage(const PageId& pageId) = 0;
  virtual void updateMainArea() = 0;
  virtual void selectFilterListRow(int row) = 0;
};

// The post-batch summary dialogs (page split, page box, content coverage,
// page size) and their remediation actions, moved verbatim out of MainWindow.
class BatchSummaries : public QObject {
  Q_OBJECT
 public:
  BatchSummaries(BatchSummariesContext& context, QWidget* dialogParent);

  void showBatchProcessingSummary(const QString& timingSummary, const QString& timingBreakdown);

  void showPageBoxSummary();

  void showContentCoverageSummary();

  void showPageSizeWarning();

  void forceTwoPageForImages(const std::vector<ImageId>& imageIds);

  void forceSinglePageForImages(const std::vector<ImageId>& imageIds);

  void preserveLayoutForPages(const std::vector<PageId>& pageIds);

  void disableAlignmentForPages(const std::vector<PageId>& pageIds);

 private:
  void jumpToPageFromSummary(const ImageId& imageId);

  void jumpToPageFromContentSummary(const PageId& pageId);

  void jumpToPageFromPageSizeWarning(const PageId& pageId);

  void goToPageSplitFromWarning();

  BatchSummariesContext& m_context;
  QWidget* m_dialogParent;
};

#endif  // SCANTAILOR_APP_BATCHSUMMARIES_H_
