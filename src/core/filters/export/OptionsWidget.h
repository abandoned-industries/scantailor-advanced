// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_EXPORT_OPTIONSWIDGET_H_
#define SCANTAILOR_EXPORT_OPTIONSWIDGET_H_

#include <QString>

#include <memory>
#include <optional>
#include <set>

#include "Dpi.h"
#include "FilterOptionsWidget.h"
#include "PageId.h"
#include "PageSelectionAccessor.h"
#include "Settings.h"
#include "ZoteroLoopSidecar.h"
#include "ui_OptionsWidget.h"

class QTimer;
class QResizeEvent;

namespace ocr {
class Settings;
}

namespace output {
class Settings;
}

class ZoteroClient;
class BookLookup;

namespace export_ {
class OptionsWidget : public FilterOptionsWidget, private Ui::ExportOptionsWidget {
  Q_OBJECT

 public:
  OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor);

  ~OptionsWidget() override;

  void setOutputSettings(std::shared_ptr<output::Settings> outputSettings);

  void setOcrSettings(std::shared_ptr<ocr::Settings> ocrSettings);

  void setProjectFilePath(const QString& projectFilePath);
  bool isZoteroLoopProject() const { return m_zoteroLoopSidecar.has_value(); }
  bool returnToZoteroEnabled() const;
  const std::optional<ZoteroLoopSidecar>& zoteroLoopSidecar() const { return m_zoteroLoopSidecar; }
  void setZoteroReturnStatus(const QString& statusText);

  void preUpdateUI(const PageInfo& pageInfo);
  void postUpdateUI(const PageId& pageId);

 signals:
  void exportToPdfRequested();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private slots:
  void noDpiLimitChanged(bool checked);
  void maxDpiChanged(int value);
  void compressGrayscaleChanged(bool checked);
  void qualityChanged(int index);
  void exportToPdfClicked();
  void changeOutputDpiClicked();
  void outputDpiChanged(const std::set<PageId>& pages, const Dpi& dpi);
  void metadataEditingFinished();
  void sendToZoteroToggled(bool checked);
  void recommendedNameToggled(bool checked);
  void guessMetadataClicked();
  void lookupIsbnClicked();

 private:
  void updateDisplay();
  void updateOutputDpiDisplay();
  void populateQualityCombo();
  void populateRoleCombo();
  void updateGuessButtonState();
  void refreshZoteroStatus();
  void updateZoteroCheckboxText();
  // Looks up isbn online and overwrites metadata fields with any non-empty
  // canonical values (keeping the isbn). Returns false on empty isbn or lookup
  // failure; the caller shows any message.
  bool applyIsbnLookup(const QString& isbn);

  std::shared_ptr<Settings> m_settings;
  std::shared_ptr<output::Settings> m_outputSettings;
  std::shared_ptr<ocr::Settings> m_ocrSettings;
  std::unique_ptr<ZoteroClient> m_zoteroClient;
  std::unique_ptr<BookLookup> m_bookLookup;
  QTimer* m_zoteroPollTimer;
  PageSelectionAccessor m_pageSelectionAccessor;
  PageId m_pageId;
  // Carries the last lookup's Result message from applyIsbnLookup to the slot
  // that decides how to present the failure.
  QString m_lastLookupMessage;
  std::optional<ZoteroLoopSidecar> m_zoteroLoopSidecar;
  bool m_hasZoteroReturnStatus = false;
};
}  // namespace export_

#endif  // SCANTAILOR_EXPORT_OPTIONSWIDGET_H_
