// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFontMetrics>
#include <QLineEdit>
#include <QMessageBox>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

#include "ApplicationSettings.h"
#include "BookLookup.h"
#include "BookMetadata.h"
#include "Dpi.h"
#include "MetadataGuesser.h"
#include "PageSequence.h"
#include "PdfExporter.h"
#include "ZoteroClient.h"
#include "filters/ocr/Settings.h"
#include "filters/output/ChangeDpiDialog.h"
#include "filters/output/Settings.h"

namespace export_ {

OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_zoteroClient(std::make_unique<ZoteroClient>(this)),
      m_bookLookup(std::make_unique<BookLookup>(this)),
      m_zoteroPollTimer(new QTimer(this)),
      m_pageSelectionAccessor(pageSelectionAccessor) {
  setupUi(this);

  populateQualityCombo();
  populateRoleCombo();

  // QLineEdit's default size hint and QFormLayout's platform-dependent field
  // growth policy otherwise propagate a wide content hint through the inner
  // scroll area.  These fields are intentionally allowed to become compact;
  // text editing remains usable because QLineEdit scrolls its contents.
  metadataFormLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  for (QWidget* field : {static_cast<QWidget*>(titleLineEdit), static_cast<QWidget*>(authorsLineEdit),
                         static_cast<QWidget*>(roleCombo), static_cast<QWidget*>(yearLineEdit),
                         static_cast<QWidget*>(publisherLineEdit), static_cast<QWidget*>(placeLineEdit),
                         static_cast<QWidget*>(isbnLineEdit), static_cast<QWidget*>(languageLineEdit)}) {
    field->setMinimumWidth(24);
    field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
  }
  roleCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  roleCombo->setMinimumContentsLength(4);

  // The scroll area's content size hint must not establish a hidden horizontal
  // overflow width when horizontal scrolling is disabled.
  settingsScrollContents->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  settingsScrollArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  for (QLabel* label : {zoteroPluginWarningLabel, zoteroStatusLabel}) {
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  }
  sendToZoteroCB->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  sendToZoteroCB->installEventFilter(this);

  // Connect UI signals
  connect(noDpiLimitCB, &QCheckBox::toggled, this, &OptionsWidget::noDpiLimitChanged);
  connect(maxDpiSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &OptionsWidget::maxDpiChanged);
  connect(compressGrayscaleCB, &QCheckBox::toggled, this, &OptionsWidget::compressGrayscaleChanged);
  connect(qualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OptionsWidget::qualityChanged);
  connect(exportToPdfBtn, &QPushButton::clicked, this, &OptionsWidget::exportToPdfClicked);
  connect(changeOutputDpiBtn, &QPushButton::clicked, this, &OptionsWidget::changeOutputDpiClicked);

  // Metadata field editing
  for (QLineEdit* edit : {titleLineEdit, authorsLineEdit, yearLineEdit, publisherLineEdit, placeLineEdit, isbnLineEdit,
                          languageLineEdit}) {
    connect(edit, &QLineEdit::editingFinished, this, &OptionsWidget::metadataEditingFinished);
  }
  connect(roleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &OptionsWidget::metadataEditingFinished);
  connect(guessMetadataBtn, &QPushButton::clicked, this, &OptionsWidget::guessMetadataClicked);
  connect(lookupIsbnBtn, &QPushButton::clicked, this, &OptionsWidget::lookupIsbnClicked);
  connect(recommendedNameCB, &QCheckBox::toggled, this, &OptionsWidget::recommendedNameToggled);
  connect(sendToZoteroCB, &QCheckBox::toggled, this, &OptionsWidget::sendToZoteroToggled);

  // Zotero status polling
  m_zoteroPollTimer->setInterval(5000);
  connect(m_zoteroPollTimer, &QTimer::timeout, this, &OptionsWidget::refreshZoteroStatus);

  updateDisplay();

  // Preserve complete non-elidable controls (notably the PDF settings
  // checkboxes) by including their now-compact content minimum and the width a
  // vertical scrollbar consumes.  The long Zotero item title is excluded via
  // the ignored policy above and is elided dynamically instead.
  settingsScrollLayout->activate();
  const QMargins panelMargins = verticalLayout->contentsMargins();
  const int scrollbarWidth = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, settingsScrollArea);
  const int styleFrameAllowance = 4 * style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr,
                                                           settingsScrollArea);
  const int settingsMinimumWidth = settingsScrollContents->minimumSizeHint().width()
                                   + panelMargins.left() + panelMargins.right() + scrollbarWidth
                                   + styleFrameAllowance;
  setMinimumWidth(qMax(250, settingsMinimumWidth));
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::setOutputSettings(std::shared_ptr<output::Settings> outputSettings) {
  m_outputSettings = std::move(outputSettings);
}

void OptionsWidget::setOcrSettings(std::shared_ptr<ocr::Settings> ocrSettings) {
  // Called during StageSequence construction, before MainWindow's thumbnail
  // sequence exists — must not touch m_pageSelectionAccessor here. The guess
  // button state is refreshed by updateDisplay() on preUpdateUI.
  m_ocrSettings = std::move(ocrSettings);
}

void OptionsWidget::setProjectFilePath(const QString& projectFilePath) {
  m_zoteroLoopSidecar = projectFilePath.isEmpty()
                              ? std::optional<ZoteroLoopSidecar>()
                              : ZoteroLoopSidecar::discover(projectFilePath);
  m_hasZoteroReturnStatus = false;
  if (m_zoteroLoopSidecar) {
    const QString foundVersion = m_zoteroLoopSidecar->pluginVersion;
    const QString minimumVersion = QString::fromLatin1(ZoteroLoopSidecar::MINIMUM_SUPPORTED_PLUGIN_VERSION);
    qInfo().noquote() << "Zotero loop project plugin version:"
                      << (foundVersion.isEmpty() ? QStringLiteral("none") : foundVersion);
    m_settings->armSendToZoteroForLoopProject();
    const QSignalBlocker blocker(sendToZoteroCB);
    sendToZoteroCB->setChecked(m_settings->sendToZotero());
    updateZoteroCheckboxText();
    const bool pluginIsOutOfDate =
        foundVersion.isEmpty()
        || ZoteroLoopSidecar::pluginVersionIsOlderThan(foundVersion, minimumVersion);
    if (pluginIsOutOfDate) {
      if (foundVersion.isEmpty()) {
        zoteroPluginWarningLabel->setText(
            tr("Your Zotero plugin is out of date — update ScanTailor Spectre Loop to %1 or newer (Help ▸ Install Zotero Plugin…).")
                .arg(minimumVersion));
      } else {
        zoteroPluginWarningLabel->setText(
            tr("Zotero plugin %1 is out of date — update ScanTailor Spectre Loop to %2 or newer (Help ▸ Install Zotero Plugin…).")
                .arg(foundVersion, minimumVersion));
      }
    }
    zoteroPluginWarningLabel->setVisible(pluginIsOutOfDate);
    zoteroStatusLabel->setText(tr("Zotero round-trip project"));
  } else {
    zoteroPluginWarningLabel->setVisible(false);
    sendToZoteroCB->setText(tr("Send to Zotero after export"));
    sendToZoteroCB->setToolTip(QString());
    zoteroStatusLabel->setText(tr("Zotero: checking…"));
  }
}

void OptionsWidget::resizeEvent(QResizeEvent* event) {
  FilterOptionsWidget::resizeEvent(event);
  updateZoteroCheckboxText();
}

bool OptionsWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == sendToZoteroCB && event->type() == QEvent::Resize) {
    updateZoteroCheckboxText();
  }
  return FilterOptionsWidget::eventFilter(watched, event);
}

void OptionsWidget::updateZoteroCheckboxText() {
  if (!m_zoteroLoopSidecar) {
    return;
  }

  const QString prefix = tr("Return to Zotero: ");
  const QString title = m_zoteroLoopSidecar->itemTitle;
  sendToZoteroCB->setToolTip(title);

  const QFontMetrics metrics(sendToZoteroCB->font());
  const int indicatorWidth = sendToZoteroCB->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr,
                                                                  sendToZoteroCB);
  const int labelSpacing = sendToZoteroCB->style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr,
                                                                sendToZoteroCB);
  const int titleWidth = qMax(0, sendToZoteroCB->contentsRect().width() - indicatorWidth - labelSpacing
                                    - metrics.horizontalAdvance(prefix));
  sendToZoteroCB->setText(prefix + metrics.elidedText(title, Qt::ElideRight, titleWidth));
}

bool OptionsWidget::returnToZoteroEnabled() const {
  return m_zoteroLoopSidecar.has_value() && m_settings->sendToZotero();
}

void OptionsWidget::setZoteroReturnStatus(const QString& statusText) {
  m_hasZoteroReturnStatus = true;
  zoteroStatusLabel->setText(statusText);
}

void OptionsWidget::showEvent(QShowEvent* event) {
  FilterOptionsWidget::showEvent(event);
  refreshZoteroStatus();
  m_zoteroPollTimer->start();
}

void OptionsWidget::hideEvent(QHideEvent* event) {
  m_zoteroPollTimer->stop();
  FilterOptionsWidget::hideEvent(event);
}

void OptionsWidget::preUpdateUI(const PageInfo& pageInfo) {
  m_pageId = pageInfo.id();
  updateDisplay();
  updateOutputDpiDisplay();
}

void OptionsWidget::postUpdateUI(const PageId& pageId) {
  // Nothing special needed for export filter
}

void OptionsWidget::populateQualityCombo() {
  qualityCombo->clear();
  qualityCombo->addItem(tr("High (95%)"), static_cast<int>(PdfExporter::Quality::High));
  qualityCombo->addItem(tr("Medium (85%)"), static_cast<int>(PdfExporter::Quality::Medium));
  qualityCombo->addItem(tr("Low (70%)"), static_cast<int>(PdfExporter::Quality::Low));
}

void OptionsWidget::populateRoleCombo() {
  roleCombo->clear();
  roleCombo->addItem(tr("Author(s)"), static_cast<int>(CreatorRole::Author));
  roleCombo->addItem(tr("Editor(s)"), static_cast<int>(CreatorRole::Editor));
  roleCombo->addItem(tr("Translator(s)"), static_cast<int>(CreatorRole::Translator));
}

void OptionsWidget::updateDisplay() {
  const QSignalBlocker blocker1(noDpiLimitCB);
  const QSignalBlocker blocker2(maxDpiSpinBox);
  const QSignalBlocker blocker3(compressGrayscaleCB);
  const QSignalBlocker blocker4(qualityCombo);

  noDpiLimitCB->setChecked(m_settings->noDpiLimit());
  maxDpiSpinBox->setValue(m_settings->maxDpi());
  maxDpiSpinBox->setEnabled(!m_settings->noDpiLimit());
  maxDpiLabel->setEnabled(!m_settings->noDpiLimit());
  compressGrayscaleCB->setChecked(m_settings->compressGrayscale());

  // Find and select the quality index
  int qualityValue = static_cast<int>(m_settings->quality());
  for (int i = 0; i < qualityCombo->count(); ++i) {
    if (qualityCombo->itemData(i).toInt() == qualityValue) {
      qualityCombo->setCurrentIndex(i);
      break;
    }
  }

  // Book metadata + Zotero + recommended-name (project-global / app-wide fields)
  const BookMetadata meta = m_settings->bookMetadata();
  {
    const QSignalBlocker b1(titleLineEdit);
    const QSignalBlocker b2(authorsLineEdit);
    const QSignalBlocker b3(yearLineEdit);
    const QSignalBlocker b4(publisherLineEdit);
    const QSignalBlocker b5(placeLineEdit);
    const QSignalBlocker b6(isbnLineEdit);
    const QSignalBlocker b7(languageLineEdit);
    const QSignalBlocker b8(sendToZoteroCB);
    const QSignalBlocker b9(recommendedNameCB);
    const QSignalBlocker b10(roleCombo);

    titleLineEdit->setText(meta.title);
    authorsLineEdit->setText(meta.authors);
    yearLineEdit->setText(meta.year);
    publisherLineEdit->setText(meta.publisher);
    placeLineEdit->setText(meta.place);
    isbnLineEdit->setText(meta.isbn);
    languageLineEdit->setText(meta.language);
    sendToZoteroCB->setChecked(m_settings->sendToZotero());
    recommendedNameCB->setChecked(ApplicationSettings::getInstance().isPdfRecommendedNameEnabled());

    const int roleValue = static_cast<int>(meta.creatorRole);
    for (int i = 0; i < roleCombo->count(); ++i) {
      if (roleCombo->itemData(i).toInt() == roleValue) {
        roleCombo->setCurrentIndex(i);
        break;
      }
    }
  }

  updateGuessButtonState();
}

void OptionsWidget::updateGuessButtonState() {
  bool enabled = false;
  if (m_ocrSettings) {
    const PageSequence pages = m_pageSelectionAccessor.allPages();
    const size_t limit = std::min<size_t>(3, pages.numPages());
    for (size_t i = 0; i < limit; ++i) {
      std::unique_ptr<ocr::OcrResult> result = m_ocrSettings->getOcrResult(pages.pageAt(i).id());
      if (result && !result->isEmpty()) {
        enabled = true;
        break;
      }
    }
  }
  guessMetadataBtn->setEnabled(enabled);
  guessMetadataBtn->setToolTip(enabled ? tr("Pre-fill empty fields from OCR of the first pages")
                                       : tr("Run OCR (stage 9) on the first pages first"));
}

void OptionsWidget::metadataEditingFinished() {
  BookMetadata meta;
  meta.title = titleLineEdit->text().trimmed();
  meta.authors = authorsLineEdit->text().trimmed();
  meta.year = yearLineEdit->text().trimmed();
  meta.publisher = publisherLineEdit->text().trimmed();
  meta.place = placeLineEdit->text().trimmed();
  meta.isbn = isbnLineEdit->text().trimmed();
  meta.language = languageLineEdit->text().trimmed();
  meta.creatorRole = static_cast<CreatorRole>(roleCombo->currentData().toInt());
  m_settings->setBookMetadata(meta);
}

void OptionsWidget::sendToZoteroToggled(bool checked) {
  m_settings->setSendToZotero(checked, true);
}

void OptionsWidget::recommendedNameToggled(bool checked) {
  ApplicationSettings::getInstance().setPdfRecommendedNameEnabled(checked);
}

void OptionsWidget::guessMetadataClicked() {
  if (!m_ocrSettings) {
    return;
  }
  const PageSequence pages = m_pageSelectionAccessor.allPages();
  const MetadataGuess guess = guessBookMetadata(*m_ocrSettings, pages);

  // "Guess from scan" is a deliberate (re)guess: overwrite each field the scan
  // could determine, but leave fields it couldn't guess untouched so a partial
  // guess never wipes existing values.
  BookMetadata meta = m_settings->bookMetadata();
  bool filledAny = false;
  if (!guess.title.isEmpty()) {
    meta.title = guess.title;
    filledAny = true;
  }
  if (!guess.author.isEmpty()) {
    meta.authors = guess.author;
    // The role travels with the author guess: an "Edited by" byline flips it to
    // Editor. Applied before the ISBN lookup, which must not touch the role.
    meta.creatorRole = guess.creatorRole;
    filledAny = true;
  }
  if (!guess.year.isEmpty()) {
    meta.year = guess.year;
    filledAny = true;
  }
  if (!guess.isbn.isEmpty()) {
    meta.isbn = guess.isbn;
    filledAny = true;
  }

  if (filledAny) {
    m_settings->setBookMetadata(meta);
    updateDisplay();
  }

  // If the guess produced an ISBN, enrich/override with canonical data. This is
  // non-fatal: on failure the OCR-guessed values remain and no error is shown.
  if (!guess.isbn.isEmpty()) {
    applyIsbnLookup(guess.isbn);
    return;
  }

  if (!filledAny) {
    QMessageBox::information(this, tr("Guess from scan"),
                             tr("Could not guess any metadata from the scanned pages."));
  }
}

bool OptionsWidget::applyIsbnLookup(const QString& isbn) {
  if (isbn.trimmed().isEmpty()) {
    return false;
  }

  BookMetadata looked;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  const BookLookup::Result result = m_bookLookup->lookupByIsbn(isbn, looked);
  QApplication::restoreOverrideCursor();

  if (!result.ok()) {
    m_lastLookupMessage = result.message;
    return false;
  }

  // Overwrite fields with any non-empty canonical values; keep the ISBN.
  BookMetadata meta = m_settings->bookMetadata();
  if (!looked.title.isEmpty()) meta.title = looked.title;
  if (!looked.authors.isEmpty()) meta.authors = looked.authors;
  if (!looked.year.isEmpty()) meta.year = looked.year;
  if (!looked.publisher.isEmpty()) meta.publisher = looked.publisher;
  if (!looked.place.isEmpty()) meta.place = looked.place;
  if (!looked.language.isEmpty()) meta.language = looked.language;

  m_settings->setBookMetadata(meta);
  updateDisplay();
  return true;
}

void OptionsWidget::lookupIsbnClicked() {
  const QString isbn = isbnLineEdit->text().trimmed();
  if (isbn.isEmpty()) {
    QMessageBox::information(this, tr("Look up ISBN"), tr("Enter or guess an ISBN first."));
    return;
  }
  m_lastLookupMessage.clear();
  if (!applyIsbnLookup(isbn)) {
    const QString message =
        m_lastLookupMessage.isEmpty() ? tr("No record found for ISBN %1.").arg(isbn) : m_lastLookupMessage;
    QMessageBox::warning(this, tr("Look up ISBN"), message);
  }
}

void OptionsWidget::refreshZoteroStatus() {
  if (m_hasZoteroReturnStatus) {
    return;
  }
  zoteroStatusLabel->setText(tr("Zotero: checking…"));
  m_zoteroClient->pingAsync([this](bool running) {
    if (!m_hasZoteroReturnStatus) {
      zoteroStatusLabel->setText(running ? tr("Zotero: running") : tr("Zotero: not running"));
    }
  });
}

void OptionsWidget::noDpiLimitChanged(bool checked) {
  m_settings->setNoDpiLimit(checked);
  maxDpiSpinBox->setEnabled(!checked);
  maxDpiLabel->setEnabled(!checked);
}

void OptionsWidget::maxDpiChanged(int value) {
  m_settings->setMaxDpi(value);
}

void OptionsWidget::compressGrayscaleChanged(bool checked) {
  m_settings->setCompressGrayscale(checked);
}

void OptionsWidget::qualityChanged(int index) {
  if (index >= 0) {
    int qualityValue = qualityCombo->itemData(index).toInt();
    m_settings->setQuality(static_cast<PdfExporter::Quality>(qualityValue));
  }
}

void OptionsWidget::exportToPdfClicked() {
  emit exportToPdfRequested();
}

void OptionsWidget::changeOutputDpiClicked() {
  if (!m_outputSettings) return;

  const output::Params params = m_outputSettings->getParams(m_pageId);
  const Dpi outputDpi = params.outputDpi();

  auto* dialog = new output::ChangeDpiDialog(this, outputDpi, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&, const Dpi&)),
          this, SLOT(outputDpiChanged(const std::set<PageId>&, const Dpi&)));
  dialog->show();
}

void OptionsWidget::outputDpiChanged(const std::set<PageId>& pages, const Dpi& dpi) {
  if (!m_outputSettings) return;

  for (const PageId& pageId : pages) {
    output::Params params = m_outputSettings->getParams(pageId);
    params.setOutputDpi(dpi);
    m_outputSettings->setParams(pageId, params);
  }
  updateOutputDpiDisplay();
}

void OptionsWidget::updateOutputDpiDisplay() {
  if (!m_outputSettings) {
    outputDpiLabel->setText("300 dpi");
    return;
  }
  const output::Params params = m_outputSettings->getParams(m_pageId);
  const Dpi dpi = params.outputDpi();
  if (dpi.horizontal() != dpi.vertical()) {
    outputDpiLabel->setText(QString("%1 x %2 dpi").arg(dpi.horizontal()).arg(dpi.vertical()));
  } else {
    outputDpiLabel->setText(QString("%1 dpi").arg(dpi.horizontal()));
  }
}

}  // namespace export_
