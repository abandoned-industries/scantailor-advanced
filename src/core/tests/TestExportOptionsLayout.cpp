// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <QApplication>
#include <QCheckBox>
#include <QDomDocument>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cstdio>
#include <memory>

#include "OptionsWidget.h"
#include "PageSelectionAccessor.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "Settings.h"
#include "filters/export/Filter.h"
#include "ui_OptionsWidget.h"

namespace {

bool viewportContains(QWidget* widget, QScrollArea* scrollArea) {
  const QRect widgetRect(widget->mapTo(scrollArea->viewport(), QPoint(0, 0)), widget->size());
  return scrollArea->viewport()->rect().contains(widgetRect);
}

int fail(const char* message) {
  std::fprintf(stderr, "Export options layout regression: %s\n", message);
  return 1;
}

bool impliesPluginIsMissing(const QString& text) {
  return text.contains(QStringLiteral("none"), Qt::CaseInsensitive)
         || text.contains(QStringLiteral("missing"), Qt::CaseInsensitive)
         || text.contains(QStringLiteral("not found"), Qt::CaseInsensitive)
         || text.contains(QStringLiteral("not installed"), Qt::CaseInsensitive);
}

bool writeSidecar(const QString& directoryPath, const QString& pluginVersionField,
                  const QString& itemTitle = QStringLiteral("Example Book")) {
  QFile sidecar(directoryPath + QStringLiteral("/.zotero-loop.json"));
  if (!sidecar.open(QIODevice::WriteOnly)) {
    return false;
  }
  const QByteArray contents = QStringLiteral(
      R"({"version":1,%1"itemKey":"ABCD1234","libraryID":1,"itemTitle":"%2","sourcePdf":"%3/source.pdf","returnUrl":"http://127.0.0.1:23119/st-spectre/return","token":"secret-token"})")
                                  .arg(pluginVersionField, itemTitle, directoryPath)
                                  .toUtf8();
  return sidecar.write(contents) == contents.size();
}

int checkLegacyZoteroChoiceMigration() {
  QTemporaryDir loopProject;
  if (!loopProject.isValid()
      || !writeSidecar(loopProject.path(), QStringLiteral("\"pluginVersion\":\"0.1.2\","))) {
    return fail("could not create the legacy-choice Zotero loop project");
  }

  const PageSelectionAccessor pageSelectionAccessor{
      std::shared_ptr<const PageSelectionProvider>()};
  const ProjectReader unusedReader{QDomDocument()};

  QDomDocument legacyDoc;
  if (!legacyDoc.setContent(QStringLiteral(
          "<filters><export sendToZotero=\"0\"/></filters>"))) {
    return fail("could not parse the legacy Export settings fixture");
  }
  export_::Filter legacyFilter(std::make_shared<ProjectPages>(), pageSelectionAccessor);
  legacyFilter.loadSettings(unusedReader, legacyDoc.documentElement());
  legacyFilter.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));
  auto* legacyCheckBox = legacyFilter.optionsWidget()->findChild<QCheckBox*>(
      QStringLiteral("sendToZoteroCB"));
  if (!legacyFilter.settings()->sendToZotero()
      || legacyFilter.settings()->hasExplicitSendToZoteroChoice()
      || !legacyCheckBox || !legacyCheckBox->isChecked()) {
    return fail("a legacy serialized default-off blocks automatic Zotero return");
  }

  QDomDocument explicitDoc;
  if (!explicitDoc.setContent(QStringLiteral(
          "<filters><export sendToZotero=\"0\" sendToZoteroExplicit=\"1\"/></filters>"))) {
    return fail("could not parse the explicit Export settings fixture");
  }
  export_::Filter explicitFilter(std::make_shared<ProjectPages>(), pageSelectionAccessor);
  explicitFilter.loadSettings(unusedReader, explicitDoc.documentElement());
  explicitFilter.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));
  auto* explicitCheckBox = explicitFilter.optionsWidget()->findChild<QCheckBox*>(
      QStringLiteral("sendToZoteroCB"));
  if (explicitFilter.settings()->sendToZotero()
      || !explicitFilter.settings()->hasExplicitSendToZoteroChoice()
      || !explicitCheckBox || explicitCheckBox->isChecked()) {
    return fail("a marked explicit Zotero opt-out is not preserved");
  }

  return 0;
}

int checkConstrainedWidth() {
  const QString longTitle = QStringLiteral(
      "A Report on the Art and Technology Program of the Los Angeles County Museum of Art: "
      "Exhibition: Art and Technology");
  const QString shortTitle = QStringLiteral("Example Book");
  const QString prefix = QStringLiteral("Return to Zotero: ");
  QTemporaryDir loopProject;
  if (!loopProject.isValid()
      || !writeSidecar(loopProject.path(), QStringLiteral("\"pluginVersion\":\"0.1.2\","), longTitle)) {
    return fail("could not create the constrained-width Zotero loop project");
  }

  auto settings = std::make_shared<export_::Settings>();
  const PageSelectionAccessor pageSelectionAccessor{
      std::shared_ptr<const PageSelectionProvider>()};
  export_::OptionsWidget options(settings, pageSelectionAccessor);
  options.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));

  const int narrowWidth = qMax(220, options.minimumWidth());
  options.resize(narrowWidth, 700);
  options.show();
  QApplication::processEvents();

  if (options.width() > 300) {
    return fail("the Export panel's compact minimum width is not sensibly narrow");
  }

  // Exercise every metadata field explicitly: each must share the form's
  // constrained width instead of retaining its editor size hint.
  for (const char* name : {"titleLineEdit", "authorsLineEdit", "roleCombo", "yearLineEdit",
                           "publisherLineEdit", "placeLineEdit", "isbnLineEdit", "languageLineEdit"}) {
    QWidget* field = options.findChild<QWidget*>(QString::fromLatin1(name));
    if (!field || field->minimumWidth() > 24) {
      return fail("a metadata editor retains a wide effective minimum");
    }
  }

  for (const char* name : {"noDpiLimitCB", "compressGrayscaleCB", "guessMetadataBtn",
                           "lookupIsbnBtn", "recommendedNameCB"}) {
    QWidget* control = options.findChild<QWidget*>(QString::fromLatin1(name));
    if (!control || control->width() < control->minimumSizeHint().width()) {
      std::fprintf(stderr, "undersized control: %s width=%d minimumHint=%d panel=%d\n", name,
                   control ? control->width() : -1,
                   control ? control->minimumSizeHint().width() : -1, options.width());
      return fail("a non-elidable settings control is narrower than its rendered minimum");
    }
  }

  const QRect panelRect = options.contentsRect();
  for (QWidget* child : options.findChildren<QWidget*>()) {
    if (!child->isVisible() || child->isWindow()) {
      continue;
    }
    const QRect childRect(child->mapTo(&options, QPoint(0, 0)), child->size());
    if (childRect.right() > panelRect.right()) {
      std::fprintf(stderr, "right overflow: %s right=%d panel=%d\n",
                   child->objectName().toUtf8().constData(), childRect.right(), panelRect.right());
      return fail("a visible child extends beyond the narrow panel's right edge");
    }
  }

  auto* returnCheckBox = options.findChild<QCheckBox*>(QStringLiteral("sendToZoteroCB"));
  auto* exportButton = options.findChild<QPushButton*>(QStringLiteral("exportToPdfBtn"));
  if (!returnCheckBox || !exportButton) {
    return fail("the Zotero return checkbox or Export PDF button is unavailable");
  }
  if (!returnCheckBox->text().startsWith(prefix)
      || returnCheckBox->text().size() >= prefix.size() + longTitle.size()
      || !returnCheckBox->text().endsWith(QChar(0x2026))) {
    std::fprintf(stderr, "rendered Zotero checkbox: [%s] (%lld chars)\n",
                 returnCheckBox->text().toUtf8().constData(),
                 static_cast<long long>(returnCheckBox->text().size()));
    return fail("the long Zotero item title is not prefix-preserving and right-elided");
  }
  if (returnCheckBox->toolTip() != longTitle) {
    return fail("the long Zotero item title is not preserved in the checkbox tooltip");
  }

  const QRect buttonRect(exportButton->mapTo(&options, QPoint(0, 0)), exportButton->size());
  if (!exportButton->isVisible() || buttonRect.right() > panelRect.right()) {
    return fail("Export PDF is not visible and within the narrow panel bounds");
  }

  if (!writeSidecar(loopProject.path(), QStringLiteral("\"pluginVersion\":\"0.1.2\","), shortTitle)) {
    return fail("could not update the constrained-width Zotero title");
  }
  options.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));
  QApplication::processEvents();
  if (returnCheckBox->text() != prefix + shortTitle
      || returnCheckBox->text().endsWith(QChar(0x2026))) {
    std::fprintf(stderr, "rendered short Zotero checkbox: [%s]\n",
                 returnCheckBox->text().toUtf8().constData());
    return fail("a short Zotero item title is misleadingly elided");
  }
  if (returnCheckBox->toolTip() != shortTitle) {
    return fail("the short Zotero item title is not preserved in the checkbox tooltip");
  }

  options.hide();
  return 0;
}

int checkPluginWarningBehavior() {
  QTemporaryDir loopProject;
  QTemporaryDir nonLoopProject;
  if (!loopProject.isValid() || !nonLoopProject.isValid()
      || !writeSidecar(loopProject.path(), QString())) {
    return fail("could not create the Zotero loop test project");
  }

  auto settings = std::make_shared<export_::Settings>();
  const PageSelectionAccessor pageSelectionAccessor{
      std::shared_ptr<const PageSelectionProvider>()};
  export_::OptionsWidget options(settings, pageSelectionAccessor);
  auto* warning = options.findChild<QLabel*>(QStringLiteral("zoteroPluginWarningLabel"));
  auto* status = options.findChild<QLabel*>(QStringLiteral("zoteroStatusLabel"));
  if (!warning || !status) {
    return fail("the Zotero warning or status label is unavailable");
  }

  options.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));
  const QString expectedUnknownVersionWarning = QStringLiteral(
      "Your Zotero plugin is out of date — update ScanTailor Spectre Loop to 0.1.2 or newer (Help ▸ Install Zotero Plugin…).");
  if (warning->isHidden() || warning->text() != expectedUnknownVersionWarning) {
    return fail("an unknown plugin version does not show the exact persistent warning");
  }
  if (impliesPluginIsMissing(warning->text())) {
    return fail("an unknown plugin version warning implies that the plugin is missing");
  }
  if (status->text() != QStringLiteral("Zotero round-trip project")) {
    return fail("a plugin warning hijacks the Zotero round-trip status line");
  }

  if (!writeSidecar(loopProject.path(), QStringLiteral("\"pluginVersion\":\"0.1.1\","))) {
    return fail("could not add an old version to the Zotero loop test sidecar");
  }
  options.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));
  const QString expectedKnownVersionWarning = QStringLiteral(
      "Zotero plugin 0.1.1 is out of date — update ScanTailor Spectre Loop to 0.1.2 or newer (Help ▸ Install Zotero Plugin…).");
  if (warning->isHidden() || warning->text() != expectedKnownVersionWarning
      || !warning->text().contains(QStringLiteral("0.1.1"))) {
    return fail("a known old plugin version does not show its version in the exact warning");
  }

  options.show();
  QApplication::processEvents();
  const QString pollingStatus = status->text();
  if (pollingStatus != QStringLiteral("Zotero: checking…")
      && pollingStatus != QStringLiteral("Zotero: running")
      && pollingStatus != QStringLiteral("Zotero: not running")) {
    return fail("a plugin warning suppresses Zotero running-status polling");
  }
  if (warning->isHidden() || warning->text() != expectedKnownVersionWarning) {
    return fail("Zotero polling overwrites the persistent plugin warning");
  }

  options.setZoteroReturnStatus(QStringLiteral("Returned to Zotero"));
  if (warning->isHidden() || warning->text() != expectedKnownVersionWarning
      || status->text() != QStringLiteral("Returned to Zotero")) {
    return fail("a Zotero return status overwrites the persistent plugin warning");
  }

  if (!writeSidecar(loopProject.path(), QStringLiteral("\"pluginVersion\":\"0.1.2\","))) {
    return fail("could not update the Zotero loop test sidecar");
  }
  options.setProjectFilePath(loopProject.filePath(QStringLiteral("project.ScanTailor")));
  if (!warning->isHidden() || status->text() != QStringLiteral("Zotero round-trip project")) {
    return fail("a current plugin version does not hide the warning without changing loop status");
  }

  options.setProjectFilePath(nonLoopProject.filePath(QStringLiteral("project.ScanTailor")));
  if (!warning->isHidden()) {
    return fail("a non-loop project shows the Zotero plugin warning");
  }
  options.hide();
  return 0;
}

int checkDockHeight(int dockHeight, bool expectOuterScroll, bool showPluginWarning = false) {
  // Reproduce MainWindow's options dock hierarchy at the requested usable
  // height: outer scroll area -> non-owning frame -> filter widget.
  QWidget dock;
  dock.resize(270, dockHeight);
  auto* dockLayout = new QVBoxLayout(&dock);
  dockLayout->setContentsMargins(0, 0, 0, 0);

  auto* outerScroll = new QScrollArea(&dock);
  outerScroll->setFrameShape(QFrame::NoFrame);
  outerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  outerScroll->setWidgetResizable(true);
  dockLayout->addWidget(outerScroll);

  auto* outerContents = new QWidget;
  auto* outerLayout = new QVBoxLayout(outerContents);
  outerLayout->setContentsMargins(0, 0, 10, 0);
  outerLayout->setSpacing(0);
  auto* optionsFrame = new QWidget(outerContents);
  auto* optionsLayout = new QStackedLayout(optionsFrame);
  outerLayout->addWidget(optionsFrame);
  outerScroll->setWidget(outerContents);

  auto settings = std::make_shared<export_::Settings>();
  const PageSelectionAccessor pageSelectionAccessor{
      std::shared_ptr<const PageSelectionProvider>()};
  auto* exportOptions = new export_::OptionsWidget(settings, pageSelectionAccessor);
  if (showPluginWarning) {
    auto* warning = exportOptions->findChild<QLabel*>(QStringLiteral("zoteroPluginWarningLabel"));
    warning->setText(QStringLiteral(
        "Your Zotero plugin is out of date — update ScanTailor Spectre Loop to 0.1.2 or newer "
        "(Help ▸ Install Zotero Plugin…)."));
    warning->setVisible(true);
  }
  optionsLayout->addWidget(exportOptions);

  auto* settingsScrollArea = exportOptions->findChild<QScrollArea*>(QStringLiteral("settingsScrollArea"));
  auto* exportFooter = exportOptions->findChild<QFrame*>(QStringLiteral("exportFooter"));
  auto* exportButton = exportOptions->findChild<QPushButton*>(QStringLiteral("exportToPdfBtn"));
  auto* zoteroStatus = exportOptions->findChild<QLabel*>(QStringLiteral("zoteroStatusLabel"));
  auto* returnCheckBox = exportOptions->findChild<QCheckBox*>(QStringLiteral("sendToZoteroCB"));
  auto* pluginWarning = exportOptions->findChild<QLabel*>(QStringLiteral("zoteroPluginWarningLabel"));
  if (!settingsScrollArea || !exportFooter || !exportButton || !zoteroStatus
      || !returnCheckBox || !pluginWarning) {
    return fail("the production Export controls are unavailable in the dock fixture");
  }

  // Replicate MainWindow's Export-only dock minimum: horizontal scrolling is
  // disabled, so the dock must widen enough for the production widget plus
  // its existing viewport chrome instead of clipping the fixed footer.
  const int dockChromeWidth = qMax(0, dock.width() - outerScroll->viewport()->width());
  dock.setMinimumWidth(exportOptions->minimumWidth() + dockChromeWidth);
  dock.resize(qMax(dock.width(), dock.minimumWidth()), dockHeight);

  dock.show();
  QApplication::processEvents();
  // Replicate MainWindow's clamped Export frame binding: normally it matches
  // the outer viewport, but it never shrinks below the panel's minimum hint.
  const int viewportHeight = outerScroll->viewport()->height();
  optionsFrame->setFixedHeight(qMax(viewportHeight, exportOptions->minimumSizeHint().height()));
  QApplication::processEvents();

  if (expectOuterScroll) {
    if (outerScroll->verticalScrollBar()->maximum() <= 0) {
      return fail("a too-short dock does not provide an outer scroll range");
    }
    outerScroll->verticalScrollBar()->setValue(outerScroll->verticalScrollBar()->maximum());
    QApplication::processEvents();
    if (!viewportContains(exportButton, outerScroll) || !exportButton->isVisible()) {
      return fail("Export PDF is not reachable by scrolling the too-short outer dock");
    }
    return 0;
  }

  if (outerScroll->verticalScrollBar()->maximum() != 0) {
    return fail("the outer dock scrolls instead of keeping the fixed footer in its viewport");
  }
  if (settingsScrollArea->verticalScrollBar()->maximum() <= 0) {
    return fail("settings and metadata do not get their own constrained-height scroll range");
  }
  if (!viewportContains(exportFooter, outerScroll)) {
    const QRect footerRect(exportFooter->mapTo(outerScroll->viewport(), QPoint(0, 0)), exportFooter->size());
    std::fprintf(stderr,
                 "viewport=%d,%d %dx%d frame=%dx%d options=%dx%d minHint=%dx%d footer=%d,%d %dx%d\n",
                 outerScroll->viewport()->rect().x(), outerScroll->viewport()->rect().y(),
                 outerScroll->viewport()->rect().width(), outerScroll->viewport()->rect().height(),
                 optionsFrame->width(), optionsFrame->height(), exportOptions->width(), exportOptions->height(),
                 exportOptions->minimumSizeHint().width(), exportOptions->minimumSizeHint().height(),
                 footerRect.x(), footerRect.y(), footerRect.width(), footerRect.height());
    return fail("the fixed footer is outside the options dock viewport");
  }
  if (!viewportContains(exportButton, outerScroll) || !exportButton->isVisible()) {
    return fail("Export PDF is not visible in the constrained options dock");
  }
  if (!viewportContains(zoteroStatus, outerScroll) || !zoteroStatus->isVisible()
      || !viewportContains(returnCheckBox, outerScroll) || !returnCheckBox->isVisible()) {
    return fail("the Zotero return state is not visible in the fixed footer");
  }
  if (showPluginWarning
      && (!viewportContains(pluginWarning, outerScroll)
          || !pluginWarning->isVisible())) {
    return fail("the persistent Zotero plugin warning is not visible in the fixed footer");
  }

  const QPoint footerBefore = exportFooter->mapTo(outerScroll->viewport(), QPoint(0, 0));
  const QPoint buttonBefore = exportButton->mapTo(outerScroll->viewport(), QPoint(0, 0));
  settingsScrollArea->verticalScrollBar()->setValue(settingsScrollArea->verticalScrollBar()->maximum());
  QApplication::processEvents();

  const QPoint footerAfter = exportFooter->mapTo(outerScroll->viewport(), QPoint(0, 0));
  const QPoint buttonAfter = exportButton->mapTo(outerScroll->viewport(), QPoint(0, 0));
  if (footerAfter.y() != footerBefore.y() || buttonAfter.y() != buttonBefore.y()) {
    std::fprintf(stderr, "footer y %d -> %d; button y %d -> %d\n",
                 footerBefore.y(), footerAfter.y(), buttonBefore.y(), buttonAfter.y());
    return fail("scrolling settings moves the footer or Export PDF button vertically");
  }
  if (!viewportContains(exportButton, outerScroll)) {
    return fail("Export PDF leaves the dock viewport after scrolling settings");
  }

  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  if (const int result = checkLegacyZoteroChoiceMigration()) {
    return result;
  }
  if (const int result = checkPluginWarningBehavior()) {
    return result;
  }
  if (const int result = checkConstrainedWidth()) {
    return result;
  }

  QWidget exportOptions;
  Ui::ExportOptionsWidget ui;
  ui.setupUi(&exportOptions);
  if (!ui.zoteroPluginWarningLabel->isHidden()) {
    return fail("the Zotero plugin warning is not hidden by default");
  }
  if (!ui.zoteroPluginWarningLabel->wordWrap() || ui.zoteroPluginWarningLabel->font().bold()
      || !ui.zoteroPluginWarningLabel->styleSheet().contains(QStringLiteral("#d17a00"))) {
    return fail("the Zotero plugin warning is not styled as a non-bold, word-wrapped amber warning");
  }
  const int warningIndex = ui.zoteroVLayout->indexOf(ui.zoteroPluginWarningLabel);
  const int statusIndex = ui.zoteroVLayout->indexOf(ui.zoteroStatusLabel);
  if (warningIndex < 0 || statusIndex != warningIndex + 1) {
    return fail("the Zotero plugin warning is not directly above the transient status line");
  }

  if (const int result = checkDockHeight(330, false)) {
    return result;
  }
  if (const int result = checkDockHeight(330, false, true)) {
    return result;
  }
  return checkDockHeight(160, true);
}
