// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "WorkerThreadPool.h"

#include <QCoreApplication>
#include <QThreadPool>
#include <utility>

#include "ApplicationSettings.h"
#include "BatchProcessingContext.h"
#include "OutOfMemoryHandler.h"

class WorkerThreadPool::TaskResultEvent : public QEvent {
 public:
  TaskResultEvent(BackgroundTaskPtr task, FilterResultPtr result)
      : QEvent(User), m_task(std::move(task)), m_result(std::move(result)) {}

  const BackgroundTaskPtr& task() const { return m_task; }

  const FilterResultPtr& result() const { return m_result; }

 private:
  BackgroundTaskPtr m_task;
  FilterResultPtr m_result;
};

class WorkerThreadPool::TaskErrorEvent : public QEvent {
 public:
  TaskErrorEvent(BackgroundTaskPtr task, QString errorMessage)
      : QEvent(static_cast<QEvent::Type>(User + 1)), m_task(std::move(task)), m_errorMessage(std::move(errorMessage)) {}

  const BackgroundTaskPtr& task() const { return m_task; }

  const QString& errorMessage() const { return m_errorMessage; }

 private:
  BackgroundTaskPtr m_task;
  QString m_errorMessage;
};


WorkerThreadPool::WorkerThreadPool(QObject* parent) : QObject(parent), m_pool(new QThreadPool(this)) {
  updateNumberOfThreads();
}

WorkerThreadPool::~WorkerThreadPool() = default;

void WorkerThreadPool::shutdown() {
  m_pool->waitForDone();
}

bool WorkerThreadPool::hasSpareCapacity() const {
  return m_pool->activeThreadCount() < m_pool->maxThreadCount();
}

void WorkerThreadPool::submitTask(const BackgroundTaskPtr& task) {
  class Runnable : public QRunnable {
   public:
    Runnable(WorkerThreadPool& owner, BackgroundTaskPtr task) : m_owner(owner), m_task(std::move(task)) {
      setAutoDelete(true);
    }

    void run() override {
      const batch_processing::TaskScope batchTaskScope(m_task->type() == BackgroundTask::BATCH);
      if (m_task->isCancelled()) {
        return;
      }

      try {
        const FilterResultPtr result((*m_task)());
        if (result) {
          QCoreApplication::postEvent(&m_owner, new TaskResultEvent(m_task, result));
        }
      } catch (const std::bad_alloc&) {
        OutOfMemoryHandler::instance().handleOutOfMemorySituation();
      } catch (const std::exception& e) {
        qWarning() << "Exception in worker thread:" << e.what();
        QCoreApplication::postEvent(&m_owner, new TaskErrorEvent(m_task, QString::fromStdString(e.what())));
      } catch (...) {
        qWarning() << "Unknown exception in worker thread";
        QCoreApplication::postEvent(&m_owner, new TaskErrorEvent(m_task, QStringLiteral("Unknown exception")));
      }
    }

   private:
    WorkerThreadPool& m_owner;
    BackgroundTaskPtr m_task;
  };


  updateNumberOfThreads();
  m_pool->start(new Runnable(*this, task));
}  // WorkerThreadPool::submitTask

void WorkerThreadPool::customEvent(QEvent* event) {
  if (auto* evt = dynamic_cast<TaskResultEvent*>(event)) {
    emit taskResult(evt->task(), evt->result());
  } else if (auto* errEvt = dynamic_cast<TaskErrorEvent*>(event)) {
    emit taskError(errEvt->task(), errEvt->errorMessage());
  }
}

void WorkerThreadPool::updateNumberOfThreads() {
  int maxThreads = QThread::idealThreadCount();
  // Restricting num of processors for 32-bit due to
  // address space constraints.
  if (sizeof(void*) <= 4) {
    maxThreads = std::min(maxThreads, 2);
  }

  int numThreads = ApplicationSettings::getInstance().getBatchProcessingThreads(maxThreads);
  bool overrideOk = false;
  const int overrideThreads = qEnvironmentVariableIntValue("SCANTAILOR_BATCH_THREADS", &overrideOk);
  if (overrideOk) {
    numThreads = overrideThreads;
  }
  numThreads = std::max(1, numThreads);
  numThreads = std::min(numThreads, maxThreads);
  m_pool->setMaxThreadCount(numThreads);
}
