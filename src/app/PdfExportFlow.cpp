// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportFlow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QtConcurrent/QtConcurrent>
#include <atomic>
#include <functional>
#include <memory>

#include "BookMetadata.h"
#include "OutputFileNameGenerator.h"
#include "PdfExporter.h"
#include "StageSequence.h"
#include "ZoteroClient.h"
#include "ZoteroLoopSidecar.h"
#include "filters/export/Filter.h"
#include "filters/export/OptionsWidget.h"
#include "filters/export/Settings.h"
#include "filters/ocr/Filter.h"
#include "filters/ocr/OcrTextCollector.h"
#include "filters/ocr/Settings.h"

namespace {

void showExportSuccessDialog(QWidget* parent, const QString& pdfPath, const QString& message) {
  QMessageBox msgBox(parent);
  msgBox.setIcon(QMessageBox::Information);
  msgBox.setWindowTitle(QObject::tr("Export to PDF"));
  msgBox.setText(message);
  QPushButton* revealButton = msgBox.addButton(QObject::tr("Reveal in Finder"), QMessageBox::ActionRole);
  msgBox.addButton(QMessageBox::Ok);
  msgBox.setDefaultButton(QMessageBox::Ok);
  msgBox.exec();
  if (msgBox.clickedButton() == revealButton) {
    QProcess::startDetached("open", {"-R", pdfPath});
  }
}

void showExportSuccessDialog(QWidget* parent, const QString& pdfPath, int pageCount, const QString& sizeStr) {
  showExportSuccessDialog(
      parent, pdfPath,
      QObject::tr("Successfully exported %1 pages to PDF.\nFile size: %2").arg(pageCount).arg(sizeStr));
}

struct PdfExportRunResult {
  bool success = false;
  bool cancelled = false;
};

PdfExportRunResult runPdfExportInBackground(
    QWidget* parent,
    int pageCount,
    const std::function<bool(const PdfExporter::ProgressCallback&)>& exportFunction) {
  QProgressDialog progressDialog(
      QObject::tr("Exporting to PDF..."), QObject::tr("Cancel"), 0, pageCount, parent);
  progressDialog.setWindowModality(Qt::WindowModal);
  progressDialog.setMinimumDuration(0);
  progressDialog.setAutoClose(false);
  progressDialog.setAutoReset(false);
  progressDialog.setValue(0);

  auto cancellationRequested = std::make_shared<std::atomic_bool>(false);
  const QMetaObject::Connection cancellationConnection
      = QObject::connect(&progressDialog, &QProgressDialog::canceled, [&] {
    cancellationRequested->store(true, std::memory_order_relaxed);
    progressDialog.setLabelText(QObject::tr("Cancelling export..."));
    progressDialog.setCancelButton(nullptr);
  });

  QFutureWatcher<bool> watcher;
  QEventLoop waitLoop;
  QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &waitLoop, &QEventLoop::quit);

  watcher.setFuture(QtConcurrent::run([&progressDialog, cancellationRequested, exportFunction]() {
    const PdfExporter::ProgressCallback progressCallback =
        [&progressDialog, cancellationRequested](int current, int total) {
          if (cancellationRequested->load(std::memory_order_relaxed)) {
            return false;
          }
          QMetaObject::invokeMethod(
              &progressDialog,
              [&progressDialog, current, total] {
                progressDialog.setMaximum(total);
                progressDialog.setValue(qMin(current, total));
                progressDialog.setLabelText(
                    QObject::tr("Exporting page %1 of %2...").arg(current).arg(total));
              },
              Qt::QueuedConnection);
          return !cancellationRequested->load(std::memory_order_relaxed);
        };
    return exportFunction(progressCallback);
  }));

  progressDialog.show();
  if (!watcher.isFinished()) {
    waitLoop.exec();
  }

  // QProgressDialog::close() emits canceled() even when the background export
  // completed successfully. Snapshot the worker result and disconnect the
  // user-cancellation handler before closing the dialog programmatically.
  const bool success = watcher.result();
  const bool cancelled = cancellationRequested->load(std::memory_order_relaxed);
  QObject::disconnect(cancellationConnection);
  progressDialog.close();
  return {success, cancelled};
}

}  // namespace

PdfExportFlow::PdfExportFlow(PdfExportFlowContext& context, QWidget* dialogParent)
    : m_context(context), m_dialogParent(dialogParent) {}

void PdfExportFlow::exportToPdf() {
  if (!m_context.isProjectLoaded()) {
    QMessageBox::warning(m_dialogParent, tr("Export to PDF"), tr("No project is loaded."));
    return;
  }

  // Get all pages in order
  const PageSequence pages = m_context.exportPageSequence();
  if (pages.numPages() == 0) {
    QMessageBox::warning(m_dialogParent, tr("Export to PDF"), tr("No pages in project."));
    return;
  }

  // Collect output file paths
  QStringList outputFiles;
  for (const PageInfo& pageInfo : pages) {
    const QString filePath = m_context.outFileNameGen().filePathFor(pageInfo.id());
    if (QFile::exists(filePath)) {
      outputFiles.append(filePath);
    }
  }

  if (outputFiles.isEmpty()) {
    QMessageBox::warning(m_dialogParent, tr("Export to PDF"),
                         tr("No output files found. Please process the pages first."));
    return;
  }

  // Create options dialog
  QDialog optionsDialog(m_dialogParent);
  optionsDialog.setWindowTitle(tr("Export to PDF"));
  optionsDialog.setModal(true);

  auto* layout = new QFormLayout(&optionsDialog);

  auto* infoLabel = new QLabel(tr("%1 pages will be exported.").arg(outputFiles.size()));
  layout->addRow(infoLabel);

  auto* qualityCombo = new QComboBox();
  qualityCombo->addItem(tr("High Quality (larger files)"), static_cast<int>(PdfExporter::Quality::High));
  qualityCombo->addItem(tr("Medium Quality (balanced)"), static_cast<int>(PdfExporter::Quality::Medium));
  qualityCombo->addItem(tr("Lower Quality (smaller files)"), static_cast<int>(PdfExporter::Quality::Low));
  qualityCombo->setCurrentIndex(1);  // Default to Medium
  layout->addRow(tr("Quality:"), qualityCombo);

  auto* qualityNote = new QLabel(tr("Quality affects color pages and grayscale (when compressed)."));
  qualityNote->setStyleSheet("color: gray; font-size: 11px;");
  layout->addRow(qualityNote);

  auto* compressGrayCheck = new QCheckBox(tr("Compress grayscale pages (smaller, slight quality loss)"));
  compressGrayCheck->setChecked(false);
  layout->addRow(compressGrayCheck);

  // Max DPI dropdown for downsampling high-res images
  auto* maxDpiCombo = new QComboBox;
  maxDpiCombo->addItem(tr("Original (no limit)"), 0);
  maxDpiCombo->addItem(tr("600 DPI"), 600);
  maxDpiCombo->addItem(tr("300 DPI (recommended)"), 300);
  maxDpiCombo->addItem(tr("150 DPI (small file)"), 150);
  maxDpiCombo->setCurrentIndex(2);  // Default to 300 DPI
  layout->addRow(tr("Max resolution:"), maxDpiCombo);

  auto* dpiNote = new QLabel(tr("Lower resolution = smaller file. 300 DPI is good for most uses."));
  dpiNote->setStyleSheet("color: gray; font-size: 11px;");
  layout->addRow(dpiNote);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, &optionsDialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &optionsDialog, &QDialog::reject);
  layout->addRow(buttons);

  if (optionsDialog.exec() != QDialog::Accepted) {
    return;
  }

  const auto quality = static_cast<PdfExporter::Quality>(qualityCombo->currentData().toInt());
  const bool compressGrayscale = compressGrayCheck->isChecked();
  const int maxDpi = maxDpiCombo->currentData().toInt();

  // Ask user where to save
  const BookMetadata legacyMeta = m_context.stages()->exportFilter()->settings()->bookMetadata();
  QString pdfPath = QFileDialog::getSaveFileName(
      m_dialogParent, tr("Export to PDF"), m_context.defaultPdfExportPath(legacyMeta), tr("PDF Files (*.pdf)"));

  if (pdfPath.isEmpty()) {
    return;
  }

  if (!pdfPath.endsWith(".pdf", Qt::CaseInsensitive)) {
    pdfPath += ".pdf";
  }

  // The File-menu export embeds stored OCR results too, so both export
  // entry points produce the same searchable PDF.
  const QMap<QString, PdfExporter::OcrTextData> ocrData = ocr::collectOcrTextData(
      pages, m_context.outFileNameGen(), *m_context.stages()->ocrFilter()->settings());
  qDebug() << "PdfExportFlow: Collected OCR data for" << ocrData.size() << "pages";

  const PdfExportRunResult exportResult = runPdfExportInBackground(
      m_dialogParent, outputFiles.size(), [=](const PdfExporter::ProgressCallback& progressCallback) {
        return PdfExporter::exportToPdf(outputFiles, pdfPath, legacyMeta.title, legacyMeta.authors, quality,
                                        compressGrayscale, maxDpi, ocrData, progressCallback);
      });

  if (exportResult.cancelled) {
    QMessageBox::information(m_dialogParent, tr("Export to PDF"), tr("Export cancelled."));
  } else if (exportResult.success) {
    QFileInfo fileInfo(pdfPath);
    const qint64 sizeBytes = fileInfo.size();
    QString sizeStr;
    if (sizeBytes >= 1024 * 1024) {
      sizeStr = QString::number(sizeBytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
      sizeStr = QString::number(sizeBytes / 1024.0, 'f', 1) + " KB";
    }
    showExportSuccessDialog(m_dialogParent, pdfPath, outputFiles.size(), sizeStr);
  } else {
    QMessageBox::critical(m_dialogParent, tr("Export to PDF"), tr("Failed to export to PDF."));
  }
}

void PdfExportFlow::exportToPdfFromFilter() {
  if (!m_context.isProjectLoaded()) {
    QMessageBox::warning(m_dialogParent, tr("Export to PDF"), tr("No project is loaded."));
    return;
  }

  // Get all pages in order
  const PageSequence pages = m_context.exportPageSequence();
  if (pages.numPages() == 0) {
    QMessageBox::warning(m_dialogParent, tr("Export to PDF"), tr("No pages in project."));
    return;
  }

  // Get settings from export filter
  const auto& exportSettings = m_context.stages()->exportFilter()->settings();
  const bool noDpiLimit = exportSettings->noDpiLimit();
  const int maxDpi = noDpiLimit ? 0 : exportSettings->maxDpi();  // 0 means no limit
  const bool compressGrayscale = exportSettings->compressGrayscale();
  const PdfExporter::Quality quality = exportSettings->quality();

  // Collect output file paths and check for unprocessed pages
  QStringList outputFiles;
  QStringList missingPages;
  for (const PageInfo& pageInfo : pages) {
    const QString filePath = m_context.outFileNameGen().filePathFor(pageInfo.id());
    if (QFile::exists(filePath)) {
      outputFiles.append(filePath);
    } else {
      missingPages.append(pageInfo.id().imageId().filePath());
    }
  }

  // If there are missing pages, offer to process them first
  if (!missingPages.isEmpty()) {
    const int missingCount = missingPages.size();
    const int reply = QMessageBox::question(
        m_dialogParent, tr("Export to PDF"),
        tr("%1 of %2 pages have not been processed yet.\n\n"
           "Would you like to process them now before exporting?")
            .arg(missingCount)
            .arg(pages.numPages()),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (reply == QMessageBox::Cancel) {
      return;
    }

    if (reply == QMessageBox::Yes) {
      // Switch to Output filter and start batch processing
      // Export filter has no batch processing, so we need to be in Output
      m_context.selectFilterListRow(m_context.stages()->outputFilterIdx());
      m_context.startBatchProcessing();
      return;
    }

    // No - export only the processed pages
    if (outputFiles.isEmpty()) {
      QMessageBox::warning(m_dialogParent, tr("Export to PDF"),
                           tr("No output files found. Please process the pages first."));
      return;
    }
  }

  const BookMetadata meta = exportSettings->bookMetadata();
  QString pdfPath = QFileDialog::getSaveFileName(
      m_dialogParent, tr("Export to PDF"), m_context.defaultPdfExportPath(meta), tr("PDF Files (*.pdf)"));
  if (pdfPath.isEmpty()) {
    return;
  }

  if (!pdfPath.endsWith(".pdf", Qt::CaseInsensitive)) {
    pdfPath += ".pdf";
  }

  // Embed the text layer for every page with a stored OCR result. The
  // ocrEnabled flag only controls whether the OCR stage runs; results that
  // already exist always reach the exported PDF.
  const auto& ocrSettings = m_context.stages()->ocrFilter()->settings();
  const QMap<QString, PdfExporter::OcrTextData> ocrData
      = ocr::collectOcrTextData(pages, m_context.outFileNameGen(), *ocrSettings);
  qDebug() << "PdfExportFlow: Collected OCR data for" << ocrData.size() << "pages";

  // Export
  const PdfExportRunResult exportResult = runPdfExportInBackground(
      m_dialogParent, outputFiles.size(), [=](const PdfExporter::ProgressCallback& progressCallback) {
        return PdfExporter::exportToPdf(outputFiles, pdfPath, meta.title, meta.authors, quality,
                                        compressGrayscale, maxDpi, ocrData, progressCallback);
      });

  if (exportResult.cancelled) {
    QMessageBox::information(m_dialogParent, tr("Export to PDF"), tr("Export cancelled."));
  } else if (exportResult.success) {
    QFileInfo fileInfo(pdfPath);
    const qint64 sizeBytes = fileInfo.size();
    QString sizeStr;
    if (sizeBytes >= 1024 * 1024) {
      sizeStr = QString::number(sizeBytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    } else {
      sizeStr = QString::number(sizeBytes / 1024.0, 'f', 1) + " KB";
    }
    QString message =
        tr("Successfully exported %1 pages to PDF.\nFile size: %2").arg(outputFiles.size()).arg(sizeStr);

    export_::OptionsWidget* exportOptions = m_context.stages()->exportFilter()->optionsWidget();
    if (exportOptions->returnToZoteroEnabled()) {
      const ZoteroLoopSidecar sidecar = *exportOptions->zoteroLoopSidecar();
      exportOptions->setZoteroReturnStatus(tr("Zotero: returning exported PDF…"));
      QPointer<export_::OptionsWidget> optionsGuard(exportOptions);
      auto* zotero = new ZoteroClient(m_dialogParent);
      zotero->returnAttachmentAsync(
          sidecar.returnUrl, sidecar.token, sidecar.itemKey, pdfPath,
          [this, zotero, optionsGuard, pdfPath, message](ZoteroClient::Result result) mutable {
            if (result.ok()) {
              message += tr("\n\nReturned to Zotero as attachment %1.").arg(result.attachmentKey);
              if (optionsGuard) {
                optionsGuard->setZoteroReturnStatus(
                    tr("Zotero: returned as attachment %1").arg(result.attachmentKey));
              }
            } else {
              message += tr("\n\nZotero: %1").arg(result.message);
              if (optionsGuard) {
                optionsGuard->setZoteroReturnStatus(tr("Zotero: %1").arg(result.message));
              }
            }
            showExportSuccessDialog(m_dialogParent, pdfPath, message);
            zotero->deleteLater();
          });
      return;
    }

    // Non-loop projects retain the existing generic Zotero item creation path.
    // Export success is never gated on Zotero; failures are soft and informational.
    if (!exportOptions->isZoteroLoopProject() && exportSettings->sendToZotero()) {
      ZoteroClient zotero;
      const ZoteroClient::Result result =
          zotero.sendBookWithAttachment(meta, static_cast<int>(outputFiles.size()), pdfPath);
      if (result.ok()) {
        message += tr("\n\nSent to Zotero.");
      } else {
        message += tr("\n\nZotero: %1 (the PDF was exported normally).").arg(result.message);
      }
    }

    showExportSuccessDialog(m_dialogParent, pdfPath, message);
  } else {
    QMessageBox::critical(m_dialogParent, tr("Export to PDF"), tr("Failed to export to PDF."));
  }
}
