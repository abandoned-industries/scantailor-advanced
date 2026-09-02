// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TempOutputCleanup.h"

#include <QCheckBox>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

#include "ApplicationSettings.h"
#include "StageSequence.h"
#include "filters/finalize/Filter.h"
#include "filters/finalize/Settings.h"

namespace temp_cleanup {

bool isSpectreTempOutputDir(const QString& path) {
  if (path.isEmpty()) {
    return false;
  }
  const QString tempPrefix = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .absoluteFilePath(QStringLiteral("scantailor-spectre-"));
  return QDir::cleanPath(path).startsWith(QDir::cleanPath(tempPrefix));
}

bool showTempCleanupWarning(QWidget* parent) {
  // Check if warning is disabled
  if (!ApplicationSettings::getInstance().isTempCleanupWarningEnabled()) {
    return true;  // Proceed without warning
  }

  QMessageBox msgBox(parent);
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setWindowTitle(QObject::tr("Output Images"));
  msgBox.setText(QObject::tr("Temporary output images will be deleted."));
  msgBox.setInformativeText(QObject::tr("You will need to rebuild the output stage to regenerate them."));

  QCheckBox* dontShowAgain = new QCheckBox(QObject::tr("Don't show this message again"), &msgBox);
  msgBox.setCheckBox(dontShowAgain);

  msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Ok);

  const int result = msgBox.exec();

  if (dontShowAgain->isChecked()) {
    ApplicationSettings::getInstance().setTempCleanupWarningEnabled(false);
  }

  return (result == QMessageBox::Ok);
}

void cleanupTempOutputFiles(StageSequence* stages, const QString& defaultOutDir, QWidget* dialogParent) {
  qDebug() << "cleanupTempOutputFiles: entering";
  if (!stages || !stages->finalizeFilter()) {
    qDebug() << "cleanupTempOutputFiles: no stages or finalize filter, returning early";
    return;
  }

  const auto& finalizeSettings = stages->finalizeFilter()->settings();
  if (!finalizeSettings) {
    qDebug() << "cleanupTempOutputFiles: no finalize settings, returning early";
    return;
  }

  // If user chose to preserve output, don't clean up
  if (finalizeSettings->preserveOutput()) {
    return;
  }

  // m_defaultOutDir is the directory actually assigned when the project was
  // created. Re-hashing m_projectFile here points at a different directory
  // after Save Project As and leaves the real temporary files behind.
  const QString tempDir = defaultOutDir;
  if (!isSpectreTempOutputDir(tempDir)) {
    return;
  }
  QDir dir(tempDir);

  // Check if temp directory exists and has files
  if (!dir.exists()) {
    return;
  }

  const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
  if (files.isEmpty()) {
    // Empty temp dir - just remove it
    dir.removeRecursively();
    return;
  }

  // Show warning if enabled
  if (!showTempCleanupWarning(dialogParent)) {
    return;  // User cancelled
  }

  // Clean up the temp directory
  dir.removeRecursively();
  qDebug() << "Cleaned up temp output directory:" << tempDir;
}

}  // namespace temp_cleanup
