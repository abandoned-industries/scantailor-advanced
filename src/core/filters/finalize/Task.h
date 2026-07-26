// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FINALIZE_TASK_H_
#define SCANTAILOR_FINALIZE_TASK_H_

#include <memory>

#include "FilterResult.h"
#include "NonCopyable.h"
#include "PageId.h"
#include "AutoColorModePolicy.h"

class AbstractOutputTask;
class TaskStatus;
class FilterData;
class QImage;
class QPolygonF;
class QRect;

namespace output {
class Settings;
}

namespace finalize {
class Filter;
class Settings;

class Task {
  DECLARE_NON_COPYABLE(Task)

 public:
  Task(std::shared_ptr<Filter> filter,
       std::shared_ptr<AbstractOutputTask> nextTask,
       std::shared_ptr<Settings> settings,
       std::shared_ptr<output::Settings> outputSettings,
       AutoColorModePolicy autoColorModePolicy,
       const PageId& pageId,
       bool batch);

  virtual ~Task();

  FilterResultPtr process(const TaskStatus& status, const FilterData& data, const QPolygonF& contentRectPhys);

 private:
  class UiUpdater;

  void detectColorMode(const QImage& image, const QRect& sourceRect, const TaskStatus& status);

  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<AbstractOutputTask> m_nextTask;
  std::shared_ptr<Settings> m_settings;
  std::shared_ptr<output::Settings> m_outputSettings;
  const AutoColorModePolicy m_autoColorModePolicy;
  PageId m_pageId;
  bool m_batchProcessing;
};
}  // namespace finalize
#endif  // SCANTAILOR_FINALIZE_TASK_H_
