// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <QColorDialog>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QDebug>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVariantMap>
#include <algorithm>
#include <utility>

#include "../../Utils.h"
#include "filters/finalize/Settings.h"
#include "weasel/GenericPanelBridge.h"
#include "weasel/PhotoAdjustmentsWebView.h"
#include "weasel/TonalCurve.h"
#include "weasel/WebOptionsPanelBase.h"
#include "ApplyColorsDialog.h"
#include "ChangeDewarpingDialog.h"
#include "ChangeDpiDialog.h"
#include "FillZoneComparator.h"
#include "OtsuBinarizationOptionsWidget.h"
#include "PictureZoneComparator.h"
#include "SauvolaBinarizationOptionsWidget.h"
#include "WolfBinarizationOptionsWidget.h"

using namespace core;

namespace output {
namespace {
finalize::ColorMode toFinalizeColorMode(ColorMode mode) {
  switch (mode) {
    case BLACK_AND_WHITE:
      return finalize::ColorMode::BlackAndWhite;
    case GRAYSCALE:
      return finalize::ColorMode::Grayscale;
    case COLOR:
    case COLOR_GRAYSCALE:
    case AUTO_DETECT:
      return finalize::ColorMode::Color;
    case MIXED:
      return finalize::ColorMode::Mixed;
    default:
      return finalize::ColorMode::Color;
  }
}

ColorMode fromFinalizeColorMode(finalize::ColorMode mode) {
  switch (mode) {
    case finalize::ColorMode::BlackAndWhite:
      return BLACK_AND_WHITE;
    case finalize::ColorMode::Grayscale:
      return GRAYSCALE;
    case finalize::ColorMode::Color:
      return COLOR;
    case finalize::ColorMode::Mixed:
      return MIXED;
    default:
      return COLOR;
  }
}

// QtWebEngine's Cocoa accessibility bridge can crash in libqcocoa when
// external AX clients query the Output options hierarchy. Keep the native
// controls active until the WebEngine path is safe again.
constexpr bool kEnableWebOptionsPanel = false;
}  // namespace

OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_despeckleLevel(1.0),
      m_lastTab(TAB_OUTPUT),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);

  tempValue->setValidator(new QIntValidator(-100, 100, tempValue));
  tintValue->setValidator(new QIntValidator(-100, 100, tintValue));
  contrastValue->setValidator(new QIntValidator(-100, 100, contrastValue));
  highlightsValue->setValidator(new QIntValidator(-100, 100, highlightsValue));
  shadowsValue->setValidator(new QIntValidator(-100, 100, shadowsValue));
  whitesValue->setValidator(new QIntValidator(-100, 100, whitesValue));
  blacksValue->setValidator(new QIntValidator(-100, 100, blacksValue));

  auto* exposureValidator = new QDoubleValidator(weasel::PhotoAdjustments::MIN_EXPOSURE,
                                                 weasel::PhotoAdjustments::MAX_EXPOSURE,
                                                 2, exposureValue);
  exposureValidator->setNotation(QDoubleValidator::StandardNotation);
  exposureValue->setValidator(exposureValidator);

  // Try to replace the native sections with a unified web panel.
  // If the panel fails to load or can't be inserted, keep the native widgets.
  if (kEnableWebOptionsPanel) {
    m_webPanel = new weasel::WebOptionsPanelBase(QStringLiteral("photo_adjustments.html"), this);
    m_webBridge = new weasel::GenericPanelBridge(this);
    m_webPanel->registerBridge(m_webBridge);
    // Fixed vertical policy: WebOptionsPanelBase::updateHeightFromContent() locks
    // the widget to the current content height. Expanding vertically would leave
    // a large gap when sections hide (e.g., Temp/Tint in Grayscale mode).
    m_webPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_webPanel->hide();
    m_webPanel->setObjectName(QStringLiteral("webOptionsPanel"));

    if (auto* parentLayout = qobject_cast<QVBoxLayout*>(layout())) {
      const int insertIdx = parentLayout->indexOf(dewarpingPanel);
      if (insertIdx >= 0) {
        parentLayout->insertWidget(insertIdx, m_webPanel);
      } else {
        parentLayout->addWidget(m_webPanel);
      }
    } else {
      layout()->addWidget(m_webPanel);
    }

    connect(m_webPanel, &weasel::WebOptionsPanelBase::loadFinished, this, [this](bool ok) {
      m_webPanelActive = ok;
      updateColorsDisplay();
    });

    // Photo Adjustments sliders
    connect(m_webBridge, &weasel::GenericPanelBridge::valueChanged,
            this, [this](const QString& id, double value) {
              weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
              if (id == "temp") adj.setTemp(value);
              else if (id == "tint") adj.setTint(value);
              else if (id == "exposure") adj.setExposure(value);
              else if (id == "contrast") adj.setContrast(value);
              else if (id == "highlights") adj.setHighlights(value);
              else if (id == "shadows") adj.setShadows(value);
              else if (id == "whites") adj.setWhites(value);
              else if (id == "blacks") adj.setBlacks(value);
              else if (id == "wienerCoef") { wienerCoefChanged(value); return; }
              else if (id == "wienerWindow") { wienerWindowSizeChanged(static_cast<int>(value)); return; }
              else if (id == "posterizeLevel") { posterizeLevelChanged(static_cast<int>(value)); return; }
              else return;
              m_colorParams.setPhotoAdjustments(adj);
              m_delayedPhotoAdjustCommit.start(100);
              m_delayedReloadRequest.start(300);
            });

    // Checkboxes
    connect(m_webBridge, &weasel::GenericPanelBridge::checkChanged,
            this, [this](const QString& id, bool checked) {
              if (id == "passThrough") {
                QSignalBlocker blocker(passThroughCheckBox);
                passThroughCheckBox->setChecked(checked);
                passThroughToggled(checked);
              } else if (id == "fillOffcut") {
                QSignalBlocker blocker(fillOffcutCB);
                fillOffcutCB->setChecked(checked);
                fillOffcutToggled(checked);
              } else if (id == "fillMargins") {
                QSignalBlocker blocker(fillMarginsCB);
                fillMarginsCB->setChecked(checked);
                fillMarginsToggled(checked);
              } else if (id == "posterize") {
                QSignalBlocker blocker(posterizeCB);
                posterizeCB->setChecked(checked);
                posterizeToggled(checked);
              } else if (id == "posterizeNorm") {
                QSignalBlocker blocker(posterizeNormalizationCB);
                posterizeNormalizationCB->setChecked(checked);
                posterizeNormalizationToggled(checked);
              } else if (id == "forceBw") {
                QSignalBlocker blocker(posterizeForceBwCB);
                posterizeForceBwCB->setChecked(checked);
                posterizeForceBwToggled(checked);
              }
            });

    // Radio buttons
    connect(m_webBridge, &weasel::GenericPanelBridge::radioChanged,
            this, [this](const QString& name, const QString& selectedId) {
              if (name == "fillingColor") {
                QSignalBlocker whiteBlocker(fillWhiteRB);
                QSignalBlocker backgroundBlocker(fillBackgroundRB);
                fillWhiteRB->setChecked(selectedId == QLatin1String("fillWhite"));
                fillBackgroundRB->setChecked(selectedId == QLatin1String("fillBackground"));
                fillingColorChanged();
              }
            });

    // Buttons
    connect(m_webBridge, &weasel::GenericPanelBridge::actionTriggered,
            this, [this](const QString& action) {
              if (action == "auto") photoAdjAutoClicked();
              else if (action == "reset") photoAdjResetClicked();
            });
  }

  m_delayedReloadRequest.setSingleShot(true);
  m_delayedSelectedPagesBatchProcessing.setSingleShot(true);
  m_delayedPhotoAdjustCommit.setSingleShot(true);

  depthPerceptionSlider->setMinimum(qRound(DepthPerception::minValue() * 10));
  depthPerceptionSlider->setMaximum(qRound(DepthPerception::maxValue() * 10));

  despeckleSlider->setMinimum(qRound(1.0 * 10));
  despeckleSlider->setMaximum(qRound(3.0 * 10));

  colorModeSelector->addItem(tr("Black and White"), BLACK_AND_WHITE);
  colorModeSelector->addItem(tr("Color"), COLOR);
  colorModeSelector->addItem(tr("Grayscale"), GRAYSCALE);
  colorModeSelector->addItem(tr("Mixed"), MIXED);

  thresholdMethodBox->addItem(tr("Otsu"), T_OTSU);
  thresholdMethodBox->addItem(tr("Sauvola"), T_SAUVOLA);
  thresholdMethodBox->addItem(tr("Wolf"), T_WOLF);
  thresholdMethodBox->addItem(tr("Bradley"), T_BRADLEY);
  thresholdMethodBox->addItem(tr("Grad"), T_GRAD);
  thresholdMethodBox->addItem(tr("EdgePlus"), T_EDGEPLUS);
  thresholdMethodBox->addItem(tr("BlurDiv"), T_BLURDIV);
  thresholdMethodBox->addItem(tr("EdgeDiv"), T_EDGEDIV);
  thresholdMethodBox->addItem(tr("Niblack"), T_NIBLACK);
  thresholdMethodBox->addItem(tr("N.I.C.K"), T_NICK);
  thresholdMethodBox->addItem(tr("Singh"), T_SINGH);
  thresholdMethodBox->addItem(tr("WAN"), T_WAN);
  thresholdMethodBox->addItem(tr("MultiScale"), T_MSCALE);
  thresholdMethodBox->addItem(tr("Robust"), T_ROBUST);
  thresholdMethodBox->addItem(tr("Gatos"), T_GATOS);
  thresholdMethodBox->addItem(tr("Window"), T_WINDOW);
  thresholdMethodBox->addItem(tr("Fox"), T_FOX);
  thresholdMethodBox->addItem(tr("Engraving"), T_ENGRAVING);
  thresholdMethodBox->addItem(tr("BiModal"), T_BIMODAL);
  thresholdMethodBox->addItem(tr("Mean"), T_MEAN);
  thresholdMethodBox->addItem(tr("Grain"), T_GRAIN);

  // Filling color radio buttons are set up in UI - White is default

  QPointer<BinarizationOptionsWidget> otsuBinarizationOptionsWidget = new OtsuBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> sauvolaBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> wolfBinarizationOptionsWidget = new WolfBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> bradleyBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> gradBinarizationOptionsWidget = new WolfBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> edgeplusBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> blurdivBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> edgedivBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  // New algorithms - Niblack uses Sauvola-style options (window size + k coefficient)
  QPointer<BinarizationOptionsWidget> niblackBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> nickBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> singhBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  // WAN, MultiScale, Robust, Gatos - all use Sauvola-style options (window size + k coefficient)
  QPointer<BinarizationOptionsWidget> wanBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> mscaleBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> robustBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> gatosBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  // Window, Fox, Engraving - all use Sauvola-style options (window size + k coefficient)
  QPointer<BinarizationOptionsWidget> windowBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> foxBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> engravingBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);
  // BiModal and Mean use Otsu-style options (no window size, just delta)
  QPointer<BinarizationOptionsWidget> bimodalBinarizationOptionsWidget
      = new OtsuBinarizationOptionsWidget(m_settings);
  QPointer<BinarizationOptionsWidget> meanBinarizationOptionsWidget
      = new OtsuBinarizationOptionsWidget(m_settings);
  // Grain uses Sauvola-style options (window size + k coefficient)
  QPointer<BinarizationOptionsWidget> grainBinarizationOptionsWidget
      = new SauvolaBinarizationOptionsWidget(m_settings);

  while (binarizationOptions->count() != 0) {
    binarizationOptions->removeWidget(binarizationOptions->widget(0));
  }
  addBinarizationOptionsWidget(otsuBinarizationOptionsWidget);
  addBinarizationOptionsWidget(sauvolaBinarizationOptionsWidget);
  addBinarizationOptionsWidget(wolfBinarizationOptionsWidget);
  addBinarizationOptionsWidget(bradleyBinarizationOptionsWidget);
  addBinarizationOptionsWidget(gradBinarizationOptionsWidget);
  addBinarizationOptionsWidget(edgeplusBinarizationOptionsWidget);
  addBinarizationOptionsWidget(blurdivBinarizationOptionsWidget);
  addBinarizationOptionsWidget(edgedivBinarizationOptionsWidget);
  addBinarizationOptionsWidget(niblackBinarizationOptionsWidget);
  addBinarizationOptionsWidget(nickBinarizationOptionsWidget);
  addBinarizationOptionsWidget(singhBinarizationOptionsWidget);
  addBinarizationOptionsWidget(wanBinarizationOptionsWidget);
  addBinarizationOptionsWidget(mscaleBinarizationOptionsWidget);
  addBinarizationOptionsWidget(robustBinarizationOptionsWidget);
  addBinarizationOptionsWidget(gatosBinarizationOptionsWidget);
  addBinarizationOptionsWidget(windowBinarizationOptionsWidget);
  addBinarizationOptionsWidget(foxBinarizationOptionsWidget);
  addBinarizationOptionsWidget(engravingBinarizationOptionsWidget);
  addBinarizationOptionsWidget(bimodalBinarizationOptionsWidget);
  addBinarizationOptionsWidget(meanBinarizationOptionsWidget);
  addBinarizationOptionsWidget(grainBinarizationOptionsWidget);
  updateBinarizationOptionsDisplay(binarizationOptions->currentIndex());

  pictureShapeSelector->addItem(tr("Off"), OFF_SHAPE);
  pictureShapeSelector->addItem(tr("Free"), FREE_SHAPE);
  pictureShapeSelector->addItem(tr("Rectangular"), RECTANGULAR_SHAPE);

  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();

  connect(binarizationOptions, SIGNAL(currentChanged(int)), this, SLOT(updateBinarizationOptionsDisplay(int)));

  setupUiConnections();
}

OptionsWidget::~OptionsWidget() {
  if (m_delayedPhotoAdjustCommit.isActive()) {
    m_delayedPhotoAdjustCommit.stop();
    commitPhotoAdjustments();
  }
}

void OptionsWidget::setFinalizeSettings(std::shared_ptr<finalize::Settings> finalizeSettings) {
  m_finalizeSettings = std::move(finalizeSettings);
}

void OptionsWidget::preUpdateUI(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  // Page change: flush any pending photo-adjust debounce against the OLD m_pageId
  // before overwriting m_colorParams with the new page's data.
  if (m_delayedPhotoAdjustCommit.isActive()) {
    m_delayedPhotoAdjustCommit.stop();
    commitPhotoAdjustments();
  }

  // Just get existing params - don't trigger color detection on page click.
  // Detection happens during batch processing or Task execution
  const Params params = m_settings->getParams(pageId);
  m_pageId = pageId;
  m_outputDpi = params.outputDpi();
  m_colorParams = params.colorParams();
  m_colorParams.setColorMode(effectiveColorMode());
  m_splittingOptions = params.splittingOptions();
  m_pictureShapeOptions = params.pictureShapeOptions();
  m_dewarpingOptions = params.dewarpingOptions();
  m_depthPerception = params.depthPerception();
  m_despeckleLevel = params.despeckleLevel();

  // Update pass-through checkbox
  const OutputProcessingParams opp = m_settings->getOutputProcessingParams(pageId);
  passThroughCheckBox->setChecked(opp.passThrough());

  updateSelectionIndicator();
  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();

  updateWebPanelState();
}

void OptionsWidget::updateWebPanelState() {
  if (!m_webBridge) {
    return;
  }

  const OutputProcessingParams opp = m_settings->getOutputProcessingParams(m_pageId);
  const ColorCommonOptions cco = m_colorParams.colorCommonOptions();
  const weasel::PhotoAdjustments& adj = m_colorParams.photoAdjustments();

  QVariantMap state;
  state[QStringLiteral("passThrough")] = opp.passThrough();
  state[QStringLiteral("fillOffcut")] = cco.fillOffcut();
  state[QStringLiteral("fillMargins")] = cco.fillMargins();
  state[QStringLiteral("fillingColor")]
      = (cco.getFillingColor() == FILL_BACKGROUND) ? QStringLiteral("fillBackground") : QStringLiteral("fillWhite");
  state[QStringLiteral("wienerCoef")] = cco.wienerCoef();
  state[QStringLiteral("wienerWindow")] = cco.wienerWindowSize();
  state[QStringLiteral("posterize")] = cco.getPosterizationOptions().isEnabled();
  state[QStringLiteral("posterizeLevel")] = cco.getPosterizationOptions().getLevel();
  state[QStringLiteral("posterizeNorm")] = cco.getPosterizationOptions().isNormalizationEnabled();
  state[QStringLiteral("forceBw")] = cco.getPosterizationOptions().isForceBlackAndWhite();
  state[QStringLiteral("temp")] = adj.temp();
  state[QStringLiteral("tint")] = adj.tint();
  state[QStringLiteral("exposure")] = adj.exposure();
  state[QStringLiteral("contrast")] = adj.contrast();
  state[QStringLiteral("highlights")] = adj.highlights();
  state[QStringLiteral("shadows")] = adj.shadows();
  state[QStringLiteral("whites")] = adj.whites();
  state[QStringLiteral("blacks")] = adj.blacks();

  const ColorMode colorMode = effectiveColorMode();
  const bool isBW = (colorMode == BLACK_AND_WHITE);
  const bool isGray = (colorMode == GRAYSCALE);
  const bool isColorOrGrayscale = (colorMode == COLOR_GRAYSCALE || colorMode == COLOR || colorMode == GRAYSCALE);
  // Keys must match DOM IDs registered via Panel.setupVisibility() in
  // src/core/weasel/webui/photo_adjustments.html — panel.js looks up setters by id.
  state[QStringLiteral("fill-color-row")] = !isBW;
  state[QStringLiteral("reduce-noise-section")] = isColorOrGrayscale && m_lastTab != TAB_DEWARPING;
  state[QStringLiteral("photo-adjustments-section")] = !isBW;
  state[QStringLiteral("white-balance-section")] = !isBW && !isGray;
  state[QStringLiteral("temp-row")] = !isBW && !isGray;
  state[QStringLiteral("tint-row")] = !isBW && !isGray;

  m_webBridge->setState(state);
}

void OptionsWidget::postUpdateUI() {
  auto block = m_connectionManager.getScopedBlock();

  m_dewarpingOptions = m_settings->getParams(m_pageId).dewarpingOptions();
  updateDewarpingDisplay();
}

void OptionsWidget::tabChanged(const ImageViewTab tab) {
  m_lastTab = tab;
  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();
  reloadIfNecessary();
}

void OptionsWidget::distortionModelChanged(const dewarping::DistortionModel& model) {
  m_settings->setDistortionModel(m_pageId, model);

  m_dewarpingOptions.setDewarpingMode(MANUAL);
  m_settings->setDewarpingOptions(m_pageId, m_dewarpingOptions);
  updateDewarpingDisplay();
}

void OptionsWidget::colorModeChanged(const int idx) {
  const int mode = colorModeSelector->itemData(idx).toInt();

  // User selected a mode from the dropdown
  m_colorParams.setColorMode((ColorMode) mode);
  m_colorParams.setColorModeUserSet(true);  // Mark as user-set to prevent auto-detection override
  m_settings->setColorParams(m_pageId, m_colorParams);
  updateColorsDisplay();

  // Also update finalize settings so finalize filter stays in sync
  if (m_finalizeSettings) {
    m_finalizeSettings->setColorMode(m_pageId, toFinalizeColorMode(static_cast<ColorMode>(mode)));
    qDebug() << "Output: User set color mode to" << mode << "for page" << m_pageId.imageId().filePath()
             << "- synced to finalize filter";
  }

  // Apply to all selected pages
  applyColorParamsToSelectedPages(true);

  emit invalidateThumbnail(m_pageId);
  emit reloadRequested();
}

void OptionsWidget::thresholdMethodChanged(int idx) {
  const BinarizationMethod method = (BinarizationMethod) thresholdMethodBox->itemData(idx).toInt();
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
  blackWhiteOptions.setBinarizationMethod(method);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit invalidateThumbnail(m_pageId);
  emit reloadRequested();
}

void OptionsWidget::fillingColorChanged() {
  // Determine color from radio buttons
  FillingColor color = FILL_WHITE;
  if (fillBackgroundRB->isChecked()) {
    color = FILL_BACKGROUND;
  }

  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillingColor(color);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit invalidateThumbnail(m_pageId);
  emit reloadRequested();
}

void OptionsWidget::pictureShapeChanged(const int idx) {
  const auto shapeMode = static_cast<PictureShape>(pictureShapeSelector->itemData(idx).toInt());
  m_pictureShapeOptions.setPictureShape(shapeMode);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  pictureShapeSensitivityOptions->setVisible(shapeMode == RECTANGULAR_SHAPE);
  higherSearchSensitivityCB->setVisible(shapeMode != OFF_SHAPE);

  emit reloadRequested();
}

void OptionsWidget::pictureShapeSensitivityChanged(int value) {
  m_pictureShapeOptions.setSensitivity(value);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::higherSearchSensivityToggled(const bool checked) {
  m_pictureShapeOptions.setHigherSearchSensitivity(checked);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  emit reloadRequested();
}

void OptionsWidget::fillMarginsToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillMargins(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::fillOffcutToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillOffcut(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::equalizeIlluminationToggled(const bool checked) {
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
  blackWhiteOptions.setNormalizeIllumination(checked);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

static void syncSliderValue(CenteredTickSlider* slider, int value) {
  if (slider->value() == value) {
    return;
  }
  slider->blockSignals(true);
  slider->setValue(value);
  slider->blockSignals(false);
}

void OptionsWidget::photoAdjTempChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(tempSlider, value);
  tempValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setTemp(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjTintChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(tintSlider, value);
  tintValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setTint(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjExposureChanged(int value) {
  value = std::clamp(value, exposureSlider->minimum(), exposureSlider->maximum());
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(exposureSlider, value);
  exposureValue->setText(QString::number(value / 100.0, 'f', 2));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setExposure(value / 100.0);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjContrastChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(contrastSlider, value);
  contrastValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setContrast(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjHighlightsChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(highlightsSlider, value);
  highlightsValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setHighlights(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjShadowsChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(shadowsSlider, value);
  shadowsValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setShadows(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjWhitesChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(whitesSlider, value);
  whitesValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setWhites(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjBlacksChanged(int value) {
  m_colorParams.setColorMode(effectiveColorMode());
  syncSliderValue(blacksSlider, value);
  blacksValue->setText(QString::number(value));
  weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
  adj.setBlacks(value);
  m_colorParams.setPhotoAdjustments(adj);
  m_delayedPhotoAdjustCommit.start(100);
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::photoAdjAutoClicked() {
  m_colorParams.setColorMode(effectiveColorMode());
  // Load the source image for this page to analyze
  const QString filePath = m_pageId.imageId().filePath();
  const QImage sourceImage(filePath);
  if (sourceImage.isNull()) return;

  const weasel::TonalCurve::AutoResult ar = weasel::TonalCurve::autoDetect(sourceImage);
  weasel::PhotoAdjustments adj;
  adj.setTemp(ar.temp);
  adj.setTint(ar.tint);
  adj.setExposure(ar.exposure);
  adj.setContrast(ar.contrast);
  adj.setHighlights(ar.highlights);
  adj.setShadows(ar.shadows);
  adj.setWhites(ar.whites);
  adj.setBlacks(ar.blacks);
  m_colorParams.setPhotoAdjustments(adj);
  m_settings->setColorParams(m_pageId, m_colorParams);
  applyPhotoAdjustmentsToSelectedPages(false);

  // Update all sliders and value labels
  tempSlider->setValue(static_cast<int>(adj.temp()));
  tintSlider->setValue(static_cast<int>(adj.tint()));
  exposureSlider->setValue(qRound(adj.exposure() * 100.0));
  contrastSlider->setValue(static_cast<int>(adj.contrast()));
  highlightsSlider->setValue(static_cast<int>(adj.highlights()));
  shadowsSlider->setValue(static_cast<int>(adj.shadows()));
  whitesSlider->setValue(static_cast<int>(adj.whites()));
  blacksSlider->setValue(static_cast<int>(adj.blacks()));

  updateWebPanelState();

  emit reloadRequested();
  emit invalidateAllThumbnails();
}

void OptionsWidget::photoAdjResetClicked() {
  m_colorParams.setColorMode(effectiveColorMode());
  weasel::PhotoAdjustments adj;  // all defaults = 0
  m_colorParams.setPhotoAdjustments(adj);
  m_settings->setColorParams(m_pageId, m_colorParams);
  applyPhotoAdjustmentsToSelectedPages(false);

  // Update slider positions
  tempSlider->setValue(0);
  tintSlider->setValue(0);
  exposureSlider->setValue(0);
  contrastSlider->setValue(0);
  highlightsSlider->setValue(0);
  shadowsSlider->setValue(0);
  whitesSlider->setValue(0);
  blacksSlider->setValue(0);

  updateWebPanelState();

  emit reloadRequested();
  emit invalidateAllThumbnails();
}

void OptionsWidget::passThroughToggled(bool checked) {
  OutputProcessingParams opp = m_settings->getOutputProcessingParams(m_pageId);
  opp.setPassThrough(checked);
  m_settings->setOutputProcessingParams(m_pageId, opp);

  // Auto-apply to all selected pages (like color mode does)
  const std::set<PageId> selectedPages = m_pageSelectionAccessor.selectedPages();
  if (selectedPages.size() > 1 && selectedPages.find(m_pageId) != selectedPages.end()) {
    for (const PageId& pageId : selectedPages) {
      if (pageId == m_pageId) continue;  // already set above
      OutputProcessingParams pageOpp = m_settings->getOutputProcessingParams(pageId);
      pageOpp.setPassThrough(checked);
      m_settings->setOutputProcessingParams(pageId, pageOpp);
    }
    emit invalidateAllThumbnails();
  } else {
    emit invalidateThumbnail(m_pageId);
  }

  // Refresh UI so controls are greyed out / re-enabled
  updateColorsDisplay();
  updateDewarpingDisplay();
  updateWebPanelState();

  emit reloadRequested();
}

void OptionsWidget::binarizationSettingsChanged() {
  emit reloadRequested();
  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::changeDpiButtonClicked() {
  auto* dialog = new ChangeDpiDialog(this, m_outputDpi, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&, const Dpi&)), this,
          SLOT(dpiChanged(const std::set<PageId>&, const Dpi&)));
  dialog->show();
}

void OptionsWidget::applyColorsButtonClicked() {
  // Get color mode from finalize settings (where it's actually set) rather than output settings
  ColorMode currentMode = m_colorParams.colorMode();
  if (m_finalizeSettings) {
    finalize::ColorMode finalizeMode = m_finalizeSettings->getColorMode(m_pageId);
    switch (finalizeMode) {
      case finalize::ColorMode::BlackAndWhite:
        currentMode = BLACK_AND_WHITE;
        break;
      case finalize::ColorMode::Grayscale:
        currentMode = GRAYSCALE;
        break;
      case finalize::ColorMode::Color:
        currentMode = COLOR;
        break;
      case finalize::ColorMode::Mixed:
        currentMode = MIXED;
        break;
      default:
        break;
    }
  }
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor,
                                       currentMode, m_settings, m_finalizeSettings);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this, SLOT(applyColorsConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::dpiChanged(const std::set<PageId>& pages, const Dpi& dpi) {
  for (const PageId& pageId : pages) {
    m_settings->setDpi(pageId, dpi);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    m_outputDpi = dpi;
    updateDpiDisplay();
    emit reloadRequested();
  }
}

void OptionsWidget::applyColorsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setColorParams(pageId, m_colorParams);
    m_settings->setPictureShapeOptions(pageId, m_pictureShapeOptions);
  }

  // Request batch processing of ALL pages (including current if present)
  if (!pages.empty()) {
    emit batchProcessingRequested(pages);
  }
}

void OptionsWidget::applySplittingButtonClicked() {
  // Get color mode from finalize settings (where it's actually set) rather than output settings
  ColorMode currentMode = m_colorParams.colorMode();
  if (m_finalizeSettings) {
    finalize::ColorMode finalizeMode = m_finalizeSettings->getColorMode(m_pageId);
    switch (finalizeMode) {
      case finalize::ColorMode::BlackAndWhite:
        currentMode = BLACK_AND_WHITE;
        break;
      case finalize::ColorMode::Grayscale:
        currentMode = GRAYSCALE;
        break;
      case finalize::ColorMode::Color:
        currentMode = COLOR;
        break;
      case finalize::ColorMode::Mixed:
        currentMode = MIXED;
        break;
      default:
        break;
    }
  }
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor,
                                       currentMode, m_settings, m_finalizeSettings);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Splitting Settings"));
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applySplittingOptionsConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::applySplittingOptionsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setSplittingOptions(pageId, m_splittingOptions);
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

void OptionsWidget::despeckleToggled(bool checked) {
  if (checked) {
    handleDespeckleLevelChange(0.1 * despeckleSlider->value());
  } else {
    handleDespeckleLevelChange(0);
  };

  despeckleSlider->setEnabled(checked);
}

void OptionsWidget::despeckleSliderReleased() {
  const double value = 0.1 * despeckleSlider->value();
  handleDespeckleLevelChange(value);
}

void OptionsWidget::despeckleSliderValueChanged(int value) {
  const double newValue = 0.1 * value;

  const QString tooltipText(QString::number(newValue));
  despeckleSlider->setToolTip(tooltipText);

  // Show the tooltip immediately.
  const QPoint center(despeckleSlider->rect().center());
  QPoint tooltipPos(despeckleSlider->mapFromGlobal(QCursor::pos()));
  tooltipPos.setY(center.y());
  tooltipPos.setX(qBound(0, tooltipPos.x(), despeckleSlider->width()));
  tooltipPos = despeckleSlider->mapToGlobal(tooltipPos);
  QToolTip::showText(tooltipPos, tooltipText, despeckleSlider);

  if (despeckleSlider->isSliderDown()) {
    return;
  }

  handleDespeckleLevelChange(newValue, true);
}

void OptionsWidget::handleDespeckleLevelChange(const double level, const bool delay) {
  m_despeckleLevel = level;
  m_settings->setDespeckleLevel(m_pageId, level);

  bool handled = false;
  emit despeckleLevelChanged(level, &handled);

  if (handled) {
    // This means we are on the "Despeckling" tab.
    emit invalidateThumbnail(m_pageId);
  } else {
    if (delay) {
      m_delayedReloadRequest.start(750);
    } else {
      emit reloadRequested();
    }
  }
}

void OptionsWidget::applyDespeckleButtonClicked() {
  // Get color mode from finalize settings (where it's actually set) rather than output settings
  ColorMode currentMode = m_colorParams.colorMode();
  if (m_finalizeSettings) {
    finalize::ColorMode finalizeMode = m_finalizeSettings->getColorMode(m_pageId);
    switch (finalizeMode) {
      case finalize::ColorMode::BlackAndWhite:
        currentMode = BLACK_AND_WHITE;
        break;
      case finalize::ColorMode::Grayscale:
        currentMode = GRAYSCALE;
        break;
      case finalize::ColorMode::Color:
        currentMode = COLOR;
        break;
      case finalize::ColorMode::Mixed:
        currentMode = MIXED;
        break;
      default:
        break;
    }
  }
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor,
                                       currentMode, m_settings, m_finalizeSettings);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Despeckling Level"));
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applyDespeckleConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::applyDespeckleConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setDespeckleLevel(pageId, m_despeckleLevel);
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

void OptionsWidget::changeDewarpingButtonClicked() {
  auto* dialog = new ChangeDewarpingDialog(this, m_pageId, m_dewarpingOptions, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&, const DewarpingOptions&)), this,
          SLOT(dewarpingChanged(const std::set<PageId>&, const DewarpingOptions&)));
  dialog->show();
}

void OptionsWidget::dewarpingChanged(const std::set<PageId>& pages, const DewarpingOptions& opt) {
  for (const PageId& pageId : pages) {
    m_settings->setDewarpingOptions(pageId, opt);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    if (m_dewarpingOptions != opt) {
      m_dewarpingOptions = opt;


      // We also have to reload if we are currently on the "Fill Zones" tab,
      // as it makes use of original <-> dewarped coordinate mapping,
      // which is too hard to update without reloading.  For consistency,
      // we reload not just on TAB_FILL_ZONES but on all tabs except TAB_DEWARPING.
      // PS: the static original <-> dewarped mappings are constructed
      // in Task::UiUpdater::updateUI().  Look for "new DewarpingPointMapper" there.
      if ((opt.dewarpingMode() == AUTO) || (m_lastTab != TAB_DEWARPING) || (opt.dewarpingMode() == MARGINAL)) {
        // Switch to the Output tab after reloading.
        m_lastTab = TAB_OUTPUT;
        // These depend on the value of m_lastTab.
        updateDpiDisplay();
        updateColorsDisplay();
        updateDewarpingDisplay();

        emit reloadRequested();
      } else {
        // This one we have to call anyway, as it depends on m_dewarpingMode.
        updateDewarpingDisplay();
      }
    }
  }
}  // OptionsWidget::dewarpingChanged

void OptionsWidget::applyDepthPerceptionButtonClicked() {
  // Get color mode from finalize settings (where it's actually set) rather than output settings
  ColorMode currentMode = m_colorParams.colorMode();
  if (m_finalizeSettings) {
    finalize::ColorMode finalizeMode = m_finalizeSettings->getColorMode(m_pageId);
    switch (finalizeMode) {
      case finalize::ColorMode::BlackAndWhite:
        currentMode = BLACK_AND_WHITE;
        break;
      case finalize::ColorMode::Grayscale:
        currentMode = GRAYSCALE;
        break;
      case finalize::ColorMode::Color:
        currentMode = COLOR;
        break;
      case finalize::ColorMode::Mixed:
        currentMode = MIXED;
        break;
      default:
        break;
    }
  }
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor,
                                       currentMode, m_settings, m_finalizeSettings);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Depth Perception"));
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applyDepthPerceptionConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::applyDepthPerceptionConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setDepthPerception(pageId, m_depthPerception);
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

void OptionsWidget::depthPerceptionChangedSlot(int val) {
  m_depthPerception.setValue(0.1 * val);
  const QString tooltipText(QString::number(m_depthPerception.value()));
  depthPerceptionSlider->setToolTip(tooltipText);

  // Show the tooltip immediately.
  const QPoint center(depthPerceptionSlider->rect().center());
  QPoint tooltipPos(depthPerceptionSlider->mapFromGlobal(QCursor::pos()));
  tooltipPos.setY(center.y());
  tooltipPos.setX(qBound(0, tooltipPos.x(), depthPerceptionSlider->width()));
  tooltipPos = depthPerceptionSlider->mapToGlobal(tooltipPos);
  QToolTip::showText(tooltipPos, tooltipText, depthPerceptionSlider);

  m_settings->setDepthPerception(m_pageId, m_depthPerception);
  // Propagate the signal.
  emit depthPerceptionChanged(m_depthPerception.value());
}

void OptionsWidget::reloadIfNecessary() {
  ZoneSet savedPictureZones;
  ZoneSet savedFillZones;
  DewarpingOptions savedDewarpingOptions;
  dewarping::DistortionModel savedDistortionModel;
  DepthPerception savedDepthPerception;
  double savedDespeckleLevel = 1.0;

  std::unique_ptr<OutputParams> outputParams(m_settings->getOutputParams(m_pageId));
  if (outputParams) {
    savedPictureZones = outputParams->pictureZones();
    savedFillZones = outputParams->fillZones();
    savedDewarpingOptions = outputParams->outputImageParams().dewarpingMode();
    savedDistortionModel = outputParams->outputImageParams().distortionModel();
    savedDepthPerception = outputParams->outputImageParams().depthPerception();
    savedDespeckleLevel = outputParams->outputImageParams().despeckleLevel();
  }

  if (!PictureZoneComparator::equal(savedPictureZones, m_settings->pictureZonesForPage(m_pageId))) {
    emit reloadRequested();
    return;
  } else if (!FillZoneComparator::equal(savedFillZones, m_settings->fillZonesForPage(m_pageId))) {
    emit reloadRequested();
    return;
  }

  const Params params(m_settings->getParams(m_pageId));

  if (savedDespeckleLevel != params.despeckleLevel()) {
    emit reloadRequested();
    return;
  }

  if ((savedDewarpingOptions.dewarpingMode() == OFF) && (params.dewarpingOptions().dewarpingMode() == OFF)) {
  } else if (savedDepthPerception.value() != params.depthPerception().value()) {
    emit reloadRequested();
    return;
  } else if ((savedDewarpingOptions.dewarpingMode() == AUTO) && (params.dewarpingOptions().dewarpingMode() == AUTO)) {
  } else if ((savedDewarpingOptions.dewarpingMode() == MARGINAL)
             && (params.dewarpingOptions().dewarpingMode() == MARGINAL)) {
  } else if (!savedDistortionModel.matches(params.distortionModel())) {
    emit reloadRequested();
    return;
  } else if ((savedDewarpingOptions.dewarpingMode() == OFF) != (params.dewarpingOptions().dewarpingMode() == OFF)) {
    emit reloadRequested();
    return;
  }
}  // OptionsWidget::reloadIfNecessary

void OptionsWidget::updateDpiDisplay() {
  if (m_outputDpi.horizontal() != m_outputDpi.vertical()) {
    dpiLabel->setText(QString::fromLatin1("%1 x %2 dpi").arg(m_outputDpi.horizontal()).arg(m_outputDpi.vertical()));
  } else {
    dpiLabel->setText(QString::fromLatin1("%1 dpi").arg(m_outputDpi.horizontal()));
  }
}

void OptionsWidget::updateColorsDisplay() {
  colorModeSelector->blockSignals(true);

  ColorMode colorMode = effectiveColorMode();
  // Map legacy COLOR_GRAYSCALE to COLOR for backward compatibility
  if (colorMode == COLOR_GRAYSCALE) {
    colorMode = COLOR;
  }
  const int colorModeIdx = colorModeSelector->findData(colorMode);
  colorModeSelector->setCurrentIndex(colorModeIdx);

  bool thresholdOptionsVisible = false;
  bool pictureShapeVisible = false;
  bool splittingOptionsVisible = false;
  switch (colorMode) {
    case MIXED:
      pictureShapeVisible = true;
      splittingOptionsVisible = true;
      // fall through
    case BLACK_AND_WHITE:
      thresholdOptionsVisible = true;
      // fall through
    case COLOR_GRAYSCALE:
    case COLOR:
    case GRAYSCALE:
      break;
    case AUTO_DETECT:
      // Should not be stored, but handle it
      break;
  }

  const bool useWebPanel = m_webPanelActive
      && (colorMode == COLOR || colorMode == COLOR_GRAYSCALE || colorMode == GRAYSCALE);

  // The web panel mirrors everything in commonOptions / colorOperationsOptions /
  // adjustmentsPanel (pass-through, fill, reduce-noise, photo adjustments). Hide
  // the native group boxes when it is active so we don't render duplicates.
  commonOptions->setVisible(!useWebPanel);
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());

  if (!blackWhiteOptions.normalizeIllumination() && colorMode == MIXED) {
    colorCommonOptions.setNormalizeIllumination(false);
  }
  m_colorParams.setColorMode(colorMode);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  fillMarginsCB->setChecked(colorCommonOptions.fillMargins());
  fillMarginsCB->setVisible(true);
  fillOffcutCB->setChecked(colorCommonOptions.fillOffcut());
  fillOffcutCB->setVisible(true);
  equalizeIlluminationCB->setChecked(blackWhiteOptions.normalizeIllumination());
  // Hide BW illumination option for color/grayscale modes
  const bool isColorOrGrayscale = (colorMode == COLOR_GRAYSCALE || colorMode == COLOR || colorMode == GRAYSCALE);
  equalizeIlluminationCB->setVisible(!isColorOrGrayscale);

  // Photo adjustments sliders
  const weasel::PhotoAdjustments& adj = m_colorParams.photoAdjustments();
  const bool isBW = (colorMode == BLACK_AND_WHITE);
  const bool isGray = (colorMode == GRAYSCALE);

  // Populate slider values and numeric labels
  tempSlider->setValue(static_cast<int>(adj.temp()));
  tempValue->setText(QString::number(static_cast<int>(adj.temp())));
  tintSlider->setValue(static_cast<int>(adj.tint()));
  tintValue->setText(QString::number(static_cast<int>(adj.tint())));
  exposureSlider->setValue(qRound(adj.exposure() * 100.0));
  exposureValue->setText(QString::number(adj.exposure(), 'f', 2));
  contrastSlider->setValue(static_cast<int>(adj.contrast()));
  contrastValue->setText(QString::number(static_cast<int>(adj.contrast())));
  highlightsSlider->setValue(static_cast<int>(adj.highlights()));
  highlightsValue->setText(QString::number(static_cast<int>(adj.highlights())));
  shadowsSlider->setValue(static_cast<int>(adj.shadows()));
  shadowsValue->setText(QString::number(static_cast<int>(adj.shadows())));
  whitesSlider->setValue(static_cast<int>(adj.whites()));
  whitesValue->setText(QString::number(static_cast<int>(adj.whites())));
  blacksSlider->setValue(static_cast<int>(adj.blacks()));
  blacksValue->setText(QString::number(static_cast<int>(adj.blacks())));

  // Temp/Tint are only meaningful for color mode.
  tempSlider->setEnabled(!isBW && !isGray);
  tintSlider->setEnabled(!isBW && !isGray);
  tempLabel->setEnabled(!isBW && !isGray);
  tintLabel->setEnabled(!isBW && !isGray);
  wbSectionLabel->setEnabled(!isBW && !isGray);
  tempLabel->setVisible(!isBW && !isGray);
  tempSlider->setVisible(!isBW && !isGray);
  tempValue->setVisible(!isBW && !isGray);
  tintLabel->setVisible(!isBW && !isGray);
  tintSlider->setVisible(!isBW && !isGray);
  tintValue->setVisible(!isBW && !isGray);
  wbSectionLabel->setVisible(!isBW && !isGray);

  // All photo adjustments disabled for B&W (binarization makes them meaningless)
  adjustmentsPanel->setVisible(!isBW && !useWebPanel);

  savitzkyGolaySmoothingCB->setChecked(blackWhiteOptions.isSavitzkyGolaySmoothingEnabled());
  savitzkyGolaySmoothingCB->setVisible(thresholdOptionsVisible);
  morphologicalSmoothingCB->setChecked(blackWhiteOptions.isMorphologicalSmoothingEnabled());
  morphologicalSmoothingCB->setVisible(thresholdOptionsVisible);

  modePanel->setVisible(m_lastTab != TAB_DEWARPING);
  pictureShapeOptions->setVisible(pictureShapeVisible);
  thresholdOptions->setVisible(thresholdOptionsVisible);
  despecklePanel->setVisible(thresholdOptionsVisible && m_lastTab != TAB_DEWARPING);
  // Color/grayscale operations only visible for those modes
  colorOperationsOptions->setVisible(isColorOrGrayscale && m_lastTab != TAB_DEWARPING && !useWebPanel);

  if (m_webPanel) {
    m_webPanel->setVisible(useWebPanel);
  }

  splittingOptions->setVisible(splittingOptionsVisible);
  splittingCB->setChecked(m_splittingOptions.isSplitOutput());
  switch (m_splittingOptions.getSplittingMode()) {
    case BLACK_AND_WHITE_FOREGROUND:
      bwForegroundRB->setChecked(true);
      break;
    case COLOR_FOREGROUND:
      colorForegroundRB->setChecked(true);
      break;
  }
  originalBackgroundCB->setChecked(m_splittingOptions.isOriginalBackgroundEnabled());
  colorForegroundRB->setEnabled(m_splittingOptions.isSplitOutput());
  bwForegroundRB->setEnabled(m_splittingOptions.isSplitOutput());
  originalBackgroundCB->setEnabled(m_splittingOptions.isSplitOutput()
                                   && (m_splittingOptions.getSplittingMode() == BLACK_AND_WHITE_FOREGROUND));

  thresholdMethodBox->setCurrentIndex((int) blackWhiteOptions.getBinarizationMethod());
  binarizationOptions->setCurrentIndex((int) blackWhiteOptions.getBinarizationMethod());

  // Set radio buttons based on filling color (visible for non-B&W modes)
  const FillingColor fillingColor = colorCommonOptions.getFillingColor();
  fillWhiteRB->setChecked(fillingColor == FILL_WHITE || fillingColor == FILL_BLACK);
  fillBackgroundRB->setChecked(fillingColor == FILL_BACKGROUND);
  fillWhiteRB->setVisible(colorMode != BLACK_AND_WHITE);
  fillBackgroundRB->setVisible(colorMode != BLACK_AND_WHITE);

  colorSegmentationCB->setVisible(thresholdOptionsVisible);
  segmenterOptionsWidget->setVisible(thresholdOptionsVisible);
  segmenterOptionsWidget->setEnabled(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  if (thresholdOptionsVisible) {
    posterizeCB->setEnabled(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
    posterizeOptionsWidget->setEnabled(blackWhiteOptions.getColorSegmenterOptions().isEnabled()
                                       && colorCommonOptions.getPosterizationOptions().isEnabled());
  } else {
    posterizeCB->setEnabled(true);
    posterizeOptionsWidget->setEnabled(colorCommonOptions.getPosterizationOptions().isEnabled());
  }
  wienerCoef->setValue(colorCommonOptions.wienerCoef());
  wienerWindowSize->setValue(colorCommonOptions.wienerWindowSize());
  colorSegmentationCB->setChecked(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  reduceNoiseSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getNoiseReduction());
  redAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getRedThresholdAdjustment());
  greenAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getGreenThresholdAdjustment());
  blueAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getBlueThresholdAdjustment());
  posterizeCB->setChecked(colorCommonOptions.getPosterizationOptions().isEnabled());
  posterizeForceBwCB->setEnabled(posterizeCB->isChecked());
  posterizeLevelSB->setValue(colorCommonOptions.getPosterizationOptions().getLevel());
  posterizeNormalizationCB->setChecked(colorCommonOptions.getPosterizationOptions().isNormalizationEnabled());
  posterizeForceBwCB->setChecked(colorCommonOptions.getPosterizationOptions().isForceBlackAndWhite());

  if (pictureShapeVisible) {
    const int pictureShapeIdx = pictureShapeSelector->findData(m_pictureShapeOptions.getPictureShape());
    pictureShapeSelector->setCurrentIndex(pictureShapeIdx);
    pictureShapeSensitivitySB->setValue(m_pictureShapeOptions.getSensitivity());
    pictureShapeSensitivityOptions->setVisible(m_pictureShapeOptions.getPictureShape() == RECTANGULAR_SHAPE);
    higherSearchSensitivityCB->setChecked(m_pictureShapeOptions.isHigherSearchSensitivity());
    higherSearchSensitivityCB->setVisible(m_pictureShapeOptions.getPictureShape() != OFF_SHAPE);
  }

  if (thresholdOptionsVisible) {
    if (m_despeckleLevel != 0) {
      despeckleCB->setChecked(true);
      despeckleSlider->setValue(qRound(10 * m_despeckleLevel));
    } else {
      despeckleCB->setChecked(false);
    }
    despeckleSlider->setEnabled(m_despeckleLevel != 0);
    despeckleSlider->setToolTip(QString::number(0.1 * despeckleSlider->value()));

    for (int i = 0; i < binarizationOptions->count(); i++) {
      auto* widget = dynamic_cast<BinarizationOptionsWidget*>(binarizationOptions->widget(i));
      widget->updateUi(m_pageId);
    }
  }

  colorModeSelector->blockSignals(false);

  // In pass-through mode, disable controls for processing that gets skipped.
  // Only DPI, brightness/contrast/auto-levels, and the pass-through checkbox itself remain active.
  // When pass-through is off, re-enable container widgets so the normal logic above takes effect.
  const bool pt = passThroughCheckBox->isChecked();
  fillMarginsCB->setEnabled(!pt);
  fillOffcutCB->setEnabled(!pt);
  equalizeIlluminationCB->setEnabled(!pt);
  savitzkyGolaySmoothingCB->setEnabled(!pt);
  morphologicalSmoothingCB->setEnabled(!pt);
  thresholdOptions->setEnabled(!pt);
  despecklePanel->setEnabled(!pt);
  splittingOptions->setEnabled(!pt);
  pictureShapeOptions->setEnabled(!pt);
  colorSegmentationCB->setEnabled(!pt);
  segmenterOptionsWidget->setEnabled(!pt && blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  posterizeCB->setEnabled(!pt);
  posterizeOptionsWidget->setEnabled(!pt);
  colorOperationsOptions->setEnabled(!pt);
  fillWhiteRB->setEnabled(!pt);
  fillBackgroundRB->setEnabled(!pt);
  // Photo adjustments stay enabled under pass-through: Task.cpp applies them to
  // the pass-through output, so sliders remain meaningful even with processing off.

  updateWebPanelState();
}  // OptionsWidget::updateColorsDisplay

void OptionsWidget::updateDewarpingDisplay() {
  depthPerceptionPanel->setVisible(m_lastTab == TAB_DEWARPING);

  switch (m_dewarpingOptions.dewarpingMode()) {
    case OFF:
      dewarpingStatusLabel->setText(tr("Off"));
      break;
    case AUTO:
      dewarpingStatusLabel->setText(tr("Auto"));
      break;
    case MANUAL:
      dewarpingStatusLabel->setText(tr("Manual"));
      break;
    case MARGINAL:
      dewarpingStatusLabel->setText(tr("Marginal"));
      break;
  }

  if ((m_dewarpingOptions.dewarpingMode() == MANUAL) || (m_dewarpingOptions.dewarpingMode() == MARGINAL)) {
    QString dewarpingStatus = dewarpingStatusLabel->text();
    if (m_dewarpingOptions.needPostDeskew()) {
      const double deskewAngle = -std::round(m_dewarpingOptions.getPostDeskewAngle() * 100) / 100;
      dewarpingStatus += " (" + tr("deskew") + ": " + QString::number(deskewAngle) + QChar(0x00B0) + ")";
    } else {
      dewarpingStatus += " (" + tr("deskew disabled") + ")";
    }
    dewarpingStatusLabel->setText(dewarpingStatus);
  }

  depthPerceptionSlider->blockSignals(true);
  depthPerceptionSlider->setValue(qRound(m_depthPerception.value() * 10));
  depthPerceptionSlider->blockSignals(false);

  // Disable dewarping controls in pass-through mode
  const bool pt = passThroughCheckBox->isChecked();
  changeDewarpingButton->setEnabled(!pt);
  depthPerceptionSlider->setEnabled(!pt);
}

void OptionsWidget::savitzkyGolaySmoothingToggled(bool checked) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setSavitzkyGolaySmoothingEnabled(checked);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::morphologicalSmoothingToggled(bool checked) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setMorphologicalSmoothingEnabled(checked);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::bwForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }

  originalBackgroundCB->setEnabled(checked);

  m_splittingOptions.setSplittingMode(BLACK_AND_WHITE_FOREGROUND);
  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::colorForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }

  originalBackgroundCB->setEnabled(!checked);

  m_splittingOptions.setSplittingMode(COLOR_FOREGROUND);
  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::splittingToggled(bool checked) {
  m_splittingOptions.setSplitOutput(checked);

  bwForegroundRB->setEnabled(checked);
  colorForegroundRB->setEnabled(checked);
  originalBackgroundCB->setEnabled(checked && bwForegroundRB->isChecked());

  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::originalBackgroundToggled(bool checked) {
  m_splittingOptions.setOriginalBackgroundEnabled(checked);

  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::wienerCoefChanged(double value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setWienerCoef(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::wienerWindowSizeChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setWienerWindowSize(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::colorSegmentationToggled(bool checked) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setEnabled(checked);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  segmenterOptionsWidget->setEnabled(checked);
  if ((m_colorParams.colorMode() == BLACK_AND_WHITE) || (m_colorParams.colorMode() == MIXED)) {
    posterizeCB->setEnabled(checked);
    posterizeOptionsWidget->setEnabled(checked && posterizeCB->isChecked());
  }

  emit reloadRequested();
}

void OptionsWidget::reduceNoiseChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setNoiseReduction(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::redAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setRedThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::greenAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setGreenThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::blueAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setBlueThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::posterizeToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setEnabled(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  posterizeOptionsWidget->setEnabled(checked);
  posterizeForceBwCB->setEnabled(checked);

  emit reloadRequested();
}

void OptionsWidget::posterizeLevelChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setLevel(value);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::posterizeNormalizationToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setNormalizationEnabled(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit reloadRequested();
}

void OptionsWidget::posterizeForceBwToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setForceBlackAndWhite(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit reloadRequested();
}

void OptionsWidget::updateBinarizationOptionsDisplay(int idx) {
  for (int i = 0; i < binarizationOptions->count(); i++) {
    QWidget* currentWidget = binarizationOptions->widget(i);
    currentWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    currentWidget->resize(0, 0);

    disconnect(currentWidget, SIGNAL(stateChanged()), this, SLOT(binarizationSettingsChanged()));
  }

  QWidget* widget = binarizationOptions->widget(idx);
  widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  widget->adjustSize();
  binarizationOptions->adjustSize();

  connect(widget, SIGNAL(stateChanged()), this, SLOT(binarizationSettingsChanged()));
}

void OptionsWidget::addBinarizationOptionsWidget(BinarizationOptionsWidget* widget) {
  widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  binarizationOptions->addWidget(widget);
}

void OptionsWidget::sendReloadRequested() {
  emit reloadRequested();
  emit invalidateAllThumbnails();
}

void OptionsWidget::sendSelectedPagesBatchProcessingRequested() {
  if (m_pendingSelectedPagesBatchProcessing.empty()) {
    return;
  }

  emit batchProcessingRequested(m_pendingSelectedPagesBatchProcessing);
  m_pendingSelectedPagesBatchProcessing.clear();
}

ColorMode OptionsWidget::effectiveColorMode() const {
  if (!m_finalizeSettings) {
    return m_colorParams.colorMode();
  }

  return fromFinalizeColorMode(m_finalizeSettings->getColorMode(m_pageId));
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidget::setupUiConnections() {
  CONNECT(passThroughCheckBox, SIGNAL(clicked(bool)), this, SLOT(passThroughToggled(bool)));
  CONNECT(changeDpiButton, SIGNAL(clicked()), this, SLOT(changeDpiButtonClicked()));
  CONNECT(colorModeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(colorModeChanged(int)));
  CONNECT(thresholdMethodBox, SIGNAL(currentIndexChanged(int)), this, SLOT(thresholdMethodChanged(int)));
  CONNECT(fillWhiteRB, SIGNAL(clicked()), this, SLOT(fillingColorChanged()));
  CONNECT(fillBackgroundRB, SIGNAL(clicked()), this, SLOT(fillingColorChanged()));
  CONNECT(pictureShapeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(pictureShapeChanged(int)));
  CONNECT(pictureShapeSensitivitySB, SIGNAL(valueChanged(int)), this, SLOT(pictureShapeSensitivityChanged(int)));
  CONNECT(higherSearchSensitivityCB, SIGNAL(clicked(bool)), this, SLOT(higherSearchSensivityToggled(bool)));

  CONNECT(wienerCoef, SIGNAL(valueChanged(double)), this, SLOT(wienerCoefChanged(double)));
  CONNECT(wienerWindowSize, SIGNAL(valueChanged(int)), this, SLOT(wienerWindowSizeChanged(int)));
  CONNECT(colorSegmentationCB, SIGNAL(clicked(bool)), this, SLOT(colorSegmentationToggled(bool)));
  CONNECT(reduceNoiseSB, SIGNAL(valueChanged(int)), this, SLOT(reduceNoiseChanged(int)));
  CONNECT(redAdjustmentSB, SIGNAL(valueChanged(int)), this, SLOT(redAdjustmentChanged(int)));
  CONNECT(greenAdjustmentSB, SIGNAL(valueChanged(int)), this, SLOT(greenAdjustmentChanged(int)));
  CONNECT(blueAdjustmentSB, SIGNAL(valueChanged(int)), this, SLOT(blueAdjustmentChanged(int)));
  CONNECT(posterizeCB, SIGNAL(clicked(bool)), this, SLOT(posterizeToggled(bool)));
  CONNECT(posterizeLevelSB, SIGNAL(valueChanged(int)), this, SLOT(posterizeLevelChanged(int)));
  CONNECT(posterizeNormalizationCB, SIGNAL(clicked(bool)), this, SLOT(posterizeNormalizationToggled(bool)));
  CONNECT(posterizeForceBwCB, SIGNAL(clicked(bool)), this, SLOT(posterizeForceBwToggled(bool)));

  CONNECT(fillMarginsCB, SIGNAL(clicked(bool)), this, SLOT(fillMarginsToggled(bool)));
  CONNECT(fillOffcutCB, SIGNAL(clicked(bool)), this, SLOT(fillOffcutToggled(bool)));
  CONNECT(equalizeIlluminationCB, SIGNAL(clicked(bool)), this, SLOT(equalizeIlluminationToggled(bool)));
  CONNECT(tempSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjTempChanged(int)));
  CONNECT(tintSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjTintChanged(int)));
  CONNECT(exposureSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjExposureChanged(int)));
  CONNECT(contrastSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjContrastChanged(int)));
  CONNECT(highlightsSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjHighlightsChanged(int)));
  CONNECT(shadowsSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjShadowsChanged(int)));
  CONNECT(whitesSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjWhitesChanged(int)));
  CONNECT(blacksSlider, SIGNAL(valueChanged(int)), this, SLOT(photoAdjBlacksChanged(int)));
  CONNECT(photoAdjAutoBtn, SIGNAL(clicked()), this, SLOT(photoAdjAutoClicked()));
  CONNECT(photoAdjResetBtn, SIGNAL(clicked()), this, SLOT(photoAdjResetClicked()));
  CONNECT(tempValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = tempValue->text().toInt(&ok);
    photoAdjTempChanged(ok ? value : tempSlider->value());
  });
  CONNECT(tintValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = tintValue->text().toInt(&ok);
    photoAdjTintChanged(ok ? value : tintSlider->value());
  });
  CONNECT(exposureValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const double value = exposureValue->text().toDouble(&ok);
    photoAdjExposureChanged(ok ? qRound(value * 100.0) : exposureSlider->value());
  });
  CONNECT(contrastValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = contrastValue->text().toInt(&ok);
    photoAdjContrastChanged(ok ? value : contrastSlider->value());
  });
  CONNECT(highlightsValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = highlightsValue->text().toInt(&ok);
    photoAdjHighlightsChanged(ok ? value : highlightsSlider->value());
  });
  CONNECT(shadowsValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = shadowsValue->text().toInt(&ok);
    photoAdjShadowsChanged(ok ? value : shadowsSlider->value());
  });
  CONNECT(whitesValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = whitesValue->text().toInt(&ok);
    photoAdjWhitesChanged(ok ? value : whitesSlider->value());
  });
  CONNECT(blacksValue, &QLineEdit::editingFinished, this, [this]() {
    bool ok = false;
    const int value = blacksValue->text().toInt(&ok);
    photoAdjBlacksChanged(ok ? value : blacksSlider->value());
  });
  CONNECT(savitzkyGolaySmoothingCB, SIGNAL(clicked(bool)), this, SLOT(savitzkyGolaySmoothingToggled(bool)));
  CONNECT(morphologicalSmoothingCB, SIGNAL(clicked(bool)), this, SLOT(morphologicalSmoothingToggled(bool)));
  CONNECT(splittingCB, SIGNAL(clicked(bool)), this, SLOT(splittingToggled(bool)));
  CONNECT(bwForegroundRB, SIGNAL(clicked(bool)), this, SLOT(bwForegroundToggled(bool)));
  CONNECT(colorForegroundRB, SIGNAL(clicked(bool)), this, SLOT(colorForegroundToggled(bool)));
  CONNECT(originalBackgroundCB, SIGNAL(clicked(bool)), this, SLOT(originalBackgroundToggled(bool)));
  CONNECT(applyColorsButton, SIGNAL(clicked()), this, SLOT(applyColorsButtonClicked()));

  CONNECT(applySplittingButton, SIGNAL(clicked()), this, SLOT(applySplittingButtonClicked()));

  CONNECT(changeDewarpingButton, SIGNAL(clicked()), this, SLOT(changeDewarpingButtonClicked()));

  CONNECT(applyDepthPerceptionButton, SIGNAL(clicked()), this, SLOT(applyDepthPerceptionButtonClicked()));

  CONNECT(despeckleCB, SIGNAL(clicked(bool)), this, SLOT(despeckleToggled(bool)));
  CONNECT(despeckleSlider, SIGNAL(sliderReleased()), this, SLOT(despeckleSliderReleased()));
  CONNECT(despeckleSlider, SIGNAL(valueChanged(int)), this, SLOT(despeckleSliderValueChanged(int)));
  CONNECT(applyDespeckleButton, SIGNAL(clicked()), this, SLOT(applyDespeckleButtonClicked()));
  CONNECT(depthPerceptionSlider, SIGNAL(valueChanged(int)), this, SLOT(depthPerceptionChangedSlot(int)));
  CONNECT(&m_delayedReloadRequest, SIGNAL(timeout()), this, SLOT(sendReloadRequested()));
  CONNECT(&m_delayedSelectedPagesBatchProcessing, SIGNAL(timeout()), this,
          SLOT(sendSelectedPagesBatchProcessingRequested()));
  connect(&m_delayedPhotoAdjustCommit, &QTimer::timeout,
          this, &OptionsWidget::commitPhotoAdjustments);

}

#undef CONNECT

ImageViewTab OptionsWidget::lastTab() const {
  return m_lastTab;
}

const DepthPerception& OptionsWidget::depthPerception() const {
  return m_depthPerception;
}

void OptionsWidget::applyColorParamsToSelectedPages(bool triggerBatchProcessing) {
  const std::set<PageId> selectedPages = m_pageSelectionAccessor.selectedPages();

  // Only apply to multiple pages if current page is in selection and there are multiple selected
  if (selectedPages.size() > 1 && selectedPages.find(m_pageId) != selectedPages.end()) {
    const ColorMode colorMode = effectiveColorMode();
    std::set<PageId> pagesToReprocess;
    m_colorParams.setColorMode(colorMode);
    m_settings->setColorParams(m_pageId, m_colorParams);

    for (const PageId& pageId : selectedPages) {
      if (pageId == m_pageId) {
        // Include current page in batch - it will be processed along with others
        pagesToReprocess.insert(pageId);
        continue;
      }

      m_settings->setColorParams(pageId, m_colorParams);
      pagesToReprocess.insert(pageId);

      // Also update finalize settings so finalize filter stays in sync
      if (m_finalizeSettings) {
        m_finalizeSettings->setColorMode(pageId, toFinalizeColorMode(colorMode));
      }
    }

    if (triggerBatchProcessing && !pagesToReprocess.empty()) {
      emit batchProcessingRequested(pagesToReprocess);
    } else if (!pagesToReprocess.empty()) {
      m_pendingSelectedPagesBatchProcessing.insert(pagesToReprocess.begin(), pagesToReprocess.end());
      m_delayedSelectedPagesBatchProcessing.start(250);
    }
  }
}

void OptionsWidget::commitPhotoAdjustments() {
  m_settings->setColorParams(m_pageId, m_colorParams);
  applyPhotoAdjustmentsToSelectedPages(false);
}

void OptionsWidget::applyPhotoAdjustmentsToSelectedPages(bool triggerBatchProcessing) {
  const std::set<PageId> selectedPages = m_pageSelectionAccessor.selectedPages();

  if (selectedPages.size() > 1 && selectedPages.find(m_pageId) != selectedPages.end()) {
    const weasel::PhotoAdjustments adj = m_colorParams.photoAdjustments();
    std::set<PageId> pagesToReprocess;
    pagesToReprocess.insert(m_pageId);  // current page already saved by caller

    for (const PageId& pageId : selectedPages) {
      if (pageId == m_pageId) {
        continue;
      }
      // Read-modify-write each target's ColorParams so we don't stomp
      // its colorMode, BW options, or ColorCommonOptions.
      ColorParams pageParams = m_settings->getParams(pageId).colorParams();
      pageParams.setPhotoAdjustments(adj);
      m_settings->setColorParams(pageId, pageParams);
      pagesToReprocess.insert(pageId);
    }

    if (triggerBatchProcessing) {
      emit batchProcessingRequested(pagesToReprocess);
    } else {
      m_pendingSelectedPagesBatchProcessing.insert(pagesToReprocess.begin(), pagesToReprocess.end());
      m_delayedSelectedPagesBatchProcessing.start(250);
    }
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

}  // namespace output
