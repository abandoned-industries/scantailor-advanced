// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Filter.h"

#include <utility>

#include "AbstractCacheDrivenOutputTask.h"
#include "AbstractOutputTask.h"

#include "CacheDrivenTask.h"
#include "FilterUiInterface.h"
#include "OptionsWidget.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "Settings.h"
#include "Task.h"
#include "filters/output/Settings.h"

namespace finalize {
Filter::Filter(std::shared_ptr<ProjectPages> pages, const PageSelectionAccessor& pageSelectionAccessor)
    : m_pages(std::move(pages)), m_settings(std::make_shared<Settings>()), m_selectedPageOrder(0) {
  m_optionsWidget.reset(new OptionsWidget(m_settings, pageSelectionAccessor));

  const PageOrderOption::ProviderPtr defaultOrder;
  m_pageOrderOptions.emplace_back(tr("Natural order"), defaultOrder);
}

Filter::~Filter() = default;

QString Filter::getName() const {
  return tr("Finalize");
}

PageView Filter::getView() const {
  return PAGE_VIEW;
}

void Filter::selected() {
  // Nothing to do
}

int Filter::selectedPageOrder() const {
  return m_selectedPageOrder;
}

void Filter::selectPageOrder(int option) {
  assert((unsigned) option < m_pageOrderOptions.size());
  m_selectedPageOrder = option;
}

std::vector<PageOrderOption> Filter::pageOrderOptions() const {
  return m_pageOrderOptions;
}

void Filter::performRelinking(const AbstractRelinker& relinker) {
  m_settings->performRelinking(relinker);
}

void Filter::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  m_optionsWidget->preUpdateUI(pageInfo);
  ui->setOptionsWidget(m_optionsWidget.get(), ui->KEEP_OWNERSHIP);
}

QDomElement Filter::saveSettings(const ProjectWriter& writer, QDomDocument& doc) const {
  QDomElement filterEl(doc.createElement("finalize"));
  filterEl.setAttribute("pictureDetectionSensitivity", m_settings->midtoneThreshold());

  writer.enumPages(
      [&](const PageId& pageId, int numericId) { this->writePageSettings(doc, filterEl, pageId, numericId); });
  return filterEl;
}

void Filter::writePageSettings(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const {
  const std::unique_ptr<Params> params(m_settings->getParams(pageId));
  if (!params) {
    return;
  }

  QDomElement pageEl(doc.createElement("page"));
  pageEl.setAttribute("id", numericId);
  pageEl.appendChild(params->toXml(doc, "params"));

  filterEl.appendChild(pageEl);
}

void Filter::loadSettings(const ProjectReader& reader, const QDomElement& filtersEl) {
  m_settings->clear();

  const QDomElement filterEl(filtersEl.namedItem("finalize").toElement());
  if (filterEl.isNull()) {
    return;
  }
  m_settings->setMidtoneThreshold(
      filterEl.attribute("pictureDetectionSensitivity", "8").toInt());

  const QString pageTagName("page");
  QDomNode node(filterEl.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != pageTagName) {
      continue;
    }
    const QDomElement el(node.toElement());

    bool ok = true;
    const int id = el.attribute("id").toInt(&ok);
    if (!ok) {
      continue;
    }

    const PageId pageId(reader.pageId(id));
    if (pageId.isNull()) {
      continue;
    }

    const QDomElement paramsEl(el.namedItem("params").toElement());
    if (paramsEl.isNull()) {
      continue;
    }

    const Params params(paramsEl);
    m_settings->setParams(pageId, params);
  }
}

void Filter::loadDefaultSettings(const PageInfo& pageInfo) {
  // No default settings needed
}

bool Filter::checkReadyForOutput(const ProjectPages& pages, const PageId* ignore) {
  const PageSequence snapshot(pages.toPageSequence(PAGE_VIEW));
  return m_settings->checkEverythingDefined(snapshot, ignore);
}

std::shared_ptr<Task> Filter::createTask(const PageId& pageId,
                                         std::shared_ptr<AbstractOutputTask> nextTask,
                                         std::shared_ptr<output::Settings> outputSettings,
                                         const bool batch) {
  return std::make_shared<Task>(std::static_pointer_cast<Filter>(shared_from_this()), std::move(nextTask), m_settings,
                                std::move(outputSettings), m_settings->autoColorModePolicy(), pageId, batch);
}

std::shared_ptr<CacheDrivenTask> Filter::createCacheDrivenTask(std::shared_ptr<AbstractCacheDrivenOutputTask> nextTask) {
  return std::make_shared<CacheDrivenTask>(std::move(nextTask), m_settings);
}

OptionsWidget* Filter::optionsWidget() {
  return m_optionsWidget.get();
}

void Filter::setOutputSettings(std::shared_ptr<output::Settings> outputSettings) {
  m_optionsWidget->setOutputSettings(std::move(outputSettings));
}
}  // namespace finalize
