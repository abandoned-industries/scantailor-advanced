// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PDFEXPORTFLOW_H_
#define SCANTAILOR_APP_PDFEXPORTFLOW_H_

#include <QObject>
#include <QString>

#include "PageSequence.h"

struct BookMetadata;
class OutputFileNameGenerator;
class StageSequence;
class QWidget;

// Narrow window-side surface the PDF export flow needs. Implemented by
// MainWindow; accessors are resolved at call time so the flow always sees
// the current project's state.
class PdfExportFlowContext {
 public:
  virtual ~PdfExportFlowContext() = default;

  virtual bool isProjectLoaded() const = 0;
  virtual PageSequence exportPageSequence() const = 0;
  virtual const OutputFileNameGenerator& outFileNameGen() const = 0;
  virtual StageSequence* stages() const = 0;
  virtual QString defaultPdfExportPath(const BookMetadata& metadata) const = 0;
  virtual void selectFilterListRow(int row) = 0;
  virtual void startBatchProcessing() = 0;
};

// The menu-driven and export-filter-driven PDF export flows, moved verbatim
// out of MainWindow (including the export-success dialogs and the
// run-in-background progress helper, which only they use).
class PdfExportFlow : public QObject {
  Q_OBJECT
 public:
  PdfExportFlow(PdfExportFlowContext& context, QWidget* dialogParent);

  void exportToPdf();

  void exportToPdfFromFilter();

 private:
  PdfExportFlowContext& m_context;
  QWidget* m_dialogParent;
};

#endif  // SCANTAILOR_APP_PDFEXPORTFLOW_H_
