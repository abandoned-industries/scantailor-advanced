// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Filter.h"

#include <filters/ocr/CacheDrivenTask.h>
#include <filters/ocr/Task.h>

#include <utility>

#include "CacheDrivenTask.h"
#include "FilterUiInterface.h"
#include "OptionsWidget.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "Settings.h"
#include "Task.h"
#include "filters/output/Settings.h"

namespace export_ {
Filter::Filter(std::shared_ptr<ProjectPages> pages, const PageSelectionAccessor& pageSelectionAccessor)
    : m_pages(std::move(pages)), m_settings(std::make_shared<Settings>()), m_selectedPageOrder(0) {
  m_optionsWidget.reset(new OptionsWidget(m_settings, pageSelectionAccessor));

  const PageOrderOption::ProviderPtr defaultOrder;
  m_pageOrderOptions.emplace_back(tr("Natural order"), defaultOrder);
}

Filter::~Filter() = default;

QString Filter::getName() const {
  return tr("Export");
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
  // Export filter doesn't need relinking - it has no per-page settings
}

void Filter::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  m_optionsWidget->preUpdateUI(pageInfo);
  ui->setOptionsWidget(m_optionsWidget.get(), ui->KEEP_OWNERSHIP);
}

QDomElement Filter::saveSettings(const ProjectWriter& writer, QDomDocument& doc) const {
  QDomElement filterEl(doc.createElement("export"));

  // Save global export settings
  filterEl.setAttribute("noDpiLimit", m_settings->noDpiLimit() ? "1" : "0");
  filterEl.setAttribute("maxDpi", m_settings->maxDpi());
  filterEl.setAttribute("compressGrayscale", m_settings->compressGrayscale() ? "1" : "0");
  filterEl.setAttribute("quality", static_cast<int>(m_settings->quality()));
  filterEl.setAttribute("sendToZotero", m_settings->sendToZotero() ? "1" : "0");
  filterEl.setAttribute("sendToZoteroExplicit", m_settings->hasExplicitSendToZoteroChoice() ? "1" : "0");

  const BookMetadata meta(m_settings->bookMetadata());
  QDomElement metadataEl(doc.createElement("metadata"));
  metadataEl.setAttribute("title", meta.title);
  metadataEl.setAttribute("authors", meta.authors);
  metadataEl.setAttribute("year", meta.year);
  metadataEl.setAttribute("publisher", meta.publisher);
  metadataEl.setAttribute("place", meta.place);
  metadataEl.setAttribute("isbn", meta.isbn);
  metadataEl.setAttribute("language", meta.language);
  metadataEl.setAttribute("creatorRole", creatorRoleToString(meta.creatorRole));
  filterEl.appendChild(metadataEl);

  return filterEl;
}

void Filter::loadSettings(const ProjectReader& reader, const QDomElement& filtersEl) {
  const QDomElement filterEl(filtersEl.namedItem("export").toElement());
  if (filterEl.isNull()) {
    m_settings->setSendToZotero(false, false);
    return;
  }

  m_settings->setNoDpiLimit(filterEl.attribute("noDpiLimit", "0") == "1");
  m_settings->setMaxDpi(filterEl.attribute("maxDpi", "400").toInt());
  m_settings->setCompressGrayscale(filterEl.attribute("compressGrayscale", "0") == "1");
  m_settings->setQuality(static_cast<PdfExporter::Quality>(filterEl.attribute("quality", "1").toInt()));
  // Builds before 2.0b1 always serialized sendToZotero="0", even when the
  // user never touched the checkbox.  The value alone therefore can't prove
  // an intentional opt-out.  Only the companion marker written by current
  // builds makes either value an explicit project choice.
  const bool hasExplicitSendToZoteroChoice = filterEl.attribute("sendToZoteroExplicit", "0") == "1";
  m_settings->setSendToZotero(filterEl.attribute("sendToZotero", "0") == "1",
                              hasExplicitSendToZoteroChoice);

  const QDomElement metadataEl(filterEl.namedItem("metadata").toElement());
  if (!metadataEl.isNull()) {
    BookMetadata meta;
    meta.title = metadataEl.attribute("title");
    meta.authors = metadataEl.attribute("authors");
    meta.year = metadataEl.attribute("year");
    meta.publisher = metadataEl.attribute("publisher");
    meta.place = metadataEl.attribute("place");
    meta.isbn = metadataEl.attribute("isbn");
    meta.language = metadataEl.attribute("language");
    meta.creatorRole = creatorRoleFromString(metadataEl.attribute("creatorRole"));
    m_settings->setBookMetadata(meta);
  }
}

void Filter::loadDefaultSettings(const PageInfo& pageInfo) {
  // No per-page default settings needed
}

std::shared_ptr<Task> Filter::createTask(const PageId& pageId,
                                         std::shared_ptr<ocr::Task> ocrTask,
                                         const bool batch) {
  return std::make_shared<Task>(std::static_pointer_cast<Filter>(shared_from_this()), std::move(ocrTask), pageId,
                                batch);
}

std::shared_ptr<CacheDrivenTask> Filter::createCacheDrivenTask(std::shared_ptr<ocr::CacheDrivenTask> ocrTask) {
  return std::make_shared<CacheDrivenTask>(std::move(ocrTask));
}

OptionsWidget* Filter::optionsWidget() {
  return m_optionsWidget.get();
}

void Filter::setOutputSettings(std::shared_ptr<output::Settings> outputSettings) {
  m_optionsWidget->setOutputSettings(std::move(outputSettings));
}

void Filter::setOcrSettings(std::shared_ptr<ocr::Settings> ocrSettings) {
  m_optionsWidget->setOcrSettings(std::move(ocrSettings));
}

void Filter::setProjectFilePath(const QString& projectFilePath) {
  m_optionsWidget->setProjectFilePath(projectFilePath);
}
}  // namespace export_
