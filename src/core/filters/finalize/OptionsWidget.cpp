// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <QDebug>
#include <QFileDialog>

#include "PageInfo.h"
#include "filters/output/ColorParams.h"
#include "filters/output/Settings.h"
#include "filters/page_layout/ApplyDialog.h"

namespace finalize {

OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)), m_pageSelectionAccessor(pageSelectionAccessor) {
  qDebug() << "Finalize OptionsWidget: constructor start";
  setupUi(this);
  qDebug() << "Finalize OptionsWidget: setupUi done";

  // Set up color mode combo box
  colorModeCombo->addItem(tr("Black and White"), static_cast<int>(ColorMode::BlackAndWhite));
  colorModeCombo->addItem(tr("Grayscale"), static_cast<int>(ColorMode::Grayscale));
  colorModeCombo->addItem(tr("Mixed"), static_cast<int>(ColorMode::Mixed));
  colorModeCombo->addItem(tr("Color"), static_cast<int>(ColorMode::Color));

  // Set up format combo box
  formatCombo->addItem(tr("TIFF"), static_cast<int>(OutputFormat::TIFF));
  formatCombo->addItem(tr("PNG"), static_cast<int>(OutputFormat::PNG));
  formatCombo->addItem(tr("JPEG"), static_cast<int>(OutputFormat::JPEG));

  // Set up compression combo box (for TIFF)
  compressionCombo->addItem(tr("LZW"), static_cast<int>(TiffCompression::LZW));
  compressionCombo->addItem(tr("Deflate"), static_cast<int>(TiffCompression::Deflate));

  connect(colorModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &OptionsWidget::colorModeChanged);
  connect(thresholdSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
          &OptionsWidget::thresholdChanged);
  connect(applyToBtn, &QPushButton::clicked, this, &OptionsWidget::applyToClicked);
  connect(clearCacheBtn, &QPushButton::clicked, this, &OptionsWidget::clearCacheClicked);
  connect(clearAllCacheBtn, &QPushButton::clicked, this, &OptionsWidget::clearAllCacheClicked);
  connect(autoWhiteBalanceCB, &QCheckBox::toggled, this, &OptionsWidget::autoWhiteBalanceChanged);

  // Output settings connections
  connect(formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OptionsWidget::formatChanged);
  connect(compressionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &OptionsWidget::compressionChanged);
  connect(jpegQualitySlider, &QSlider::valueChanged, this, &OptionsWidget::jpegQualityChanged);

  // Initialize threshold from settings
  thresholdSpinBox->setValue(m_settings->midtoneThreshold());

  // Initialize auto white balance from settings
  autoWhiteBalanceCB->setChecked(m_settings->autoWhiteBalance());

  qDebug() << "Finalize OptionsWidget: about to call updateOutputUI";

  // Initialize output settings UI
  updateOutputUI();
  qDebug() << "Finalize OptionsWidget: constructor complete";
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::setOutputSettings(std::shared_ptr<output::Settings> outputSettings) {
  m_outputSettings = std::move(outputSettings);
}

void OptionsWidget::preUpdateUI(const PageInfo& pageInfo) {
  m_pageId = pageInfo.id();
  updateDisplay();
  updateSelectionIndicator();
}

void OptionsWidget::postUpdateUI(const PageId& pageId) {
  m_pageId = pageId;
  updateDisplay();
}

void OptionsWidget::updateDisplay() {
  // Block signals while updating UI
  const bool blocked = colorModeCombo->blockSignals(true);

  const ColorMode mode = m_settings->getColorMode(m_pageId);
  const int index = colorModeCombo->findData(static_cast<int>(mode));
  if (index >= 0) {
    colorModeCombo->setCurrentIndex(index);
  }

  // Show detection status
  const std::unique_ptr<Params> params = m_settings->getParams(m_pageId);
  if (params && params->isColorModeDetected()) {
    statusLabel->setText(tr("Auto-detected"));
  } else {
    statusLabel->setText(tr("Not yet processed"));
  }

  colorModeCombo->blockSignals(blocked);
}

void OptionsWidget::colorModeChanged(int index) {
  if (m_pageId.isNull()) {
    return;
  }

  const ColorMode mode = static_cast<ColorMode>(colorModeCombo->itemData(index).toInt());

  // Apply to all selected pages if multiple are selected
  applyColorModeToSelectedPages(mode);

  emit reloadRequested();
}

void OptionsWidget::applyColorModeToSelectedPages(ColorMode mode) {
  const std::set<PageId> selectedPages = m_pageSelectionAccessor.selectedPages();

  // Determine which pages to update
  std::set<PageId> pagesToUpdate;
  if (selectedPages.size() > 1 && selectedPages.find(m_pageId) != selectedPages.end()) {
    pagesToUpdate = selectedPages;
  } else {
    pagesToUpdate.insert(m_pageId);
  }

  // Apply to all target pages
  for (const PageId& pageId : pagesToUpdate) {
    m_settings->setColorMode(pageId, mode);

    // Also update output::Settings so the output filter uses this mode
    if (m_outputSettings) {
      output::ColorMode outputMode;
      switch (mode) {
        case ColorMode::BlackAndWhite:
          outputMode = output::BLACK_AND_WHITE;
          break;
        case ColorMode::Grayscale:
          outputMode = output::GRAYSCALE;
          break;
        case ColorMode::Color:
          outputMode = output::COLOR;
          break;
        case ColorMode::Mixed:
          outputMode = output::MIXED;
          break;
        default:
          outputMode = output::COLOR;
          break;
      }
      // Read-modify-write the page's existing ColorParams so we don't stomp
      // its BW options, ColorCommonOptions, or photo adjustments.
      output::ColorParams colorParams = m_outputSettings->getParams(pageId).colorParams();
      colorParams.setColorMode(outputMode);
      colorParams.setColorModeUserSet(true);
      m_outputSettings->setColorParams(pageId, colorParams);
    }
  }

  if (pagesToUpdate.size() > 1) {
    qDebug() << "Finalize: User set color mode to" << static_cast<int>(mode) << "for" << pagesToUpdate.size()
             << "pages";
    emit invalidateAllThumbnails();
    // Also explicitly invalidate current page to ensure it gets proper thumbnail treatment
    emit invalidateThumbnail(m_pageId);
  } else {
    qDebug() << "Finalize: User set color mode to" << static_cast<int>(mode) << "for page"
             << m_pageId.imageId().filePath();
    emit invalidateThumbnail(m_pageId);
  }
}

void OptionsWidget::updateSelectionIndicator() {
  const std::set<PageId> selectedPages = m_pageSelectionAccessor.selectedPages();
  if (selectedPages.size() > 1 && selectedPages.find(m_pageId) != selectedPages.end()) {
    selectionIndicatorLabel->setText(tr("Editing %1 pages").arg(selectedPages.size()));
    selectionIndicatorLabel->setStyleSheet("QLabel { color: #666; font-weight: 500; }");
    selectionIndicatorLabel->show();
  } else {
    selectionIndicatorLabel->hide();
  }
}

void OptionsWidget::thresholdChanged(int value) {
  m_settings->setMidtoneThreshold(value);
  qDebug() << "Picture detection sensitivity changed to" << value;
}

void OptionsWidget::applyToClicked() {
  auto* dialog = new page_layout::ApplyDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Color Mode"));
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this, SLOT(applyToConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::applyToConfirmed(const std::set<PageId>& pages) {
  const ColorMode mode = m_settings->getColorMode(m_pageId);

  for (const PageId& pageId : pages) {
    m_settings->setColorMode(pageId, mode);

    // Also update output::Settings so the output filter uses this mode
    if (m_outputSettings) {
      output::ColorMode outputMode;
      switch (mode) {
        case ColorMode::BlackAndWhite:
          outputMode = output::BLACK_AND_WHITE;
          break;
        case ColorMode::Grayscale:
          outputMode = output::GRAYSCALE;
          break;
        case ColorMode::Color:
          outputMode = output::COLOR;
          break;
        case ColorMode::Mixed:
          outputMode = output::MIXED;
          break;
        default:
          outputMode = output::COLOR;
          break;
      }
      // Read-modify-write the page's existing ColorParams so we don't stomp
      // its BW options, ColorCommonOptions, or photo adjustments.
      output::ColorParams colorParams = m_outputSettings->getParams(pageId).colorParams();
      colorParams.setColorMode(outputMode);
      colorParams.setColorModeUserSet(true);
      m_outputSettings->setColorParams(pageId, colorParams);
    }
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    emit reloadRequested();
  }
}

void OptionsWidget::clearCacheClicked() {
  if (m_pageId.isNull()) {
    return;
  }

  qDebug() << "Clearing detection cache for page:" << m_pageId.imageId().filePath();
  m_settings->clearDetectionCacheForPage(m_pageId);
  updateDisplay();

  emit invalidateThumbnail(m_pageId);
  emit reloadRequested();
}

void OptionsWidget::clearAllCacheClicked() {
  qDebug() << "Clearing detection cache for ALL pages";
  m_settings->clearDetectionCache();

  emit invalidateAllThumbnails();
  emit reloadRequested();
}

void OptionsWidget::autoWhiteBalanceChanged(bool checked) {
  m_settings->setAutoWhiteBalance(checked);

  // Also sync to output::Settings so OutputGenerator uses the same setting
  if (m_outputSettings) {
    m_outputSettings->setAutoWhiteBalance(checked);
  }

  qDebug() << "Auto white balance changed to" << checked;

  // Clear detection cache when white balance setting changes,
  // since color mode detection depends on this
  m_settings->clearDetectionCache();
  emit invalidateAllThumbnails();
  emit reloadRequested();  // Force re-detection of current page
}

void OptionsWidget::formatChanged(int index) {
  const OutputFormat format = static_cast<OutputFormat>(formatCombo->itemData(index).toInt());
  m_settings->setOutputFormat(format);
  updateFormatOptions();
  emit outputFormatSettingChanged(static_cast<int>(format));
  qDebug() << "Output format changed to" << index;
}

void OptionsWidget::compressionChanged(int index) {
  const TiffCompression compression = static_cast<TiffCompression>(compressionCombo->itemData(index).toInt());
  m_settings->setTiffCompression(compression);
  emit tiffCompressionSettingChanged(static_cast<int>(compression));
  qDebug() << "TIFF compression changed to" << index;
}

void OptionsWidget::jpegQualityChanged(int value) {
  m_settings->setJpegQuality(value);
  jpegQualityValueLabel->setText(QString::number(value));
  emit jpegQualitySettingChanged(value);
  qDebug() << "JPEG quality changed to" << value;
}

void OptionsWidget::updateOutputUI() {
  // Block signals while updating UI
  const bool formatBlocked = formatCombo->blockSignals(true);
  const bool compressionBlocked = compressionCombo->blockSignals(true);
  const bool qualityBlocked = jpegQualitySlider->blockSignals(true);

  // Update format combo
  const int formatIndex = formatCombo->findData(static_cast<int>(m_settings->outputFormat()));
  if (formatIndex >= 0) {
    formatCombo->setCurrentIndex(formatIndex);
  }

  // Update compression combo
  const int compressionIndex = compressionCombo->findData(static_cast<int>(m_settings->tiffCompression()));
  if (compressionIndex >= 0) {
    compressionCombo->setCurrentIndex(compressionIndex);
  }

  // Update JPEG quality
  jpegQualitySlider->setValue(m_settings->jpegQuality());
  jpegQualityValueLabel->setText(QString::number(m_settings->jpegQuality()));

  // Show/hide format-specific options
  updateFormatOptions();

  // Restore signal blocking
  formatCombo->blockSignals(formatBlocked);
  compressionCombo->blockSignals(compressionBlocked);
  jpegQualitySlider->blockSignals(qualityBlocked);
}

void OptionsWidget::updateFormatOptions() {
  const OutputFormat format = m_settings->outputFormat();

  // Show compression options only for TIFF
  const bool showCompression = (format == OutputFormat::TIFF);
  compressionLabel->setVisible(showCompression);
  compressionCombo->setVisible(showCompression);

  // Show quality slider only for JPEG
  const bool showQuality = (format == OutputFormat::JPEG);
  jpegQualityLabel->setVisible(showQuality);
  jpegQualitySlider->setVisible(showQuality);
  jpegQualityValueLabel->setVisible(showQuality);
}

}  // namespace finalize
