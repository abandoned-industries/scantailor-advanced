// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AppController.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QTimer>

#include "MainWindow.h"
#include "PdfImportDialog.h"
#include "PdfReadError.h"
#include "PdfReader.h"
#include "ProjectCreationContext.h"
#include "ProjectFolder.h"
#include "StartupWindow.h"
#include "ZoteroLoopSidecar.h"

AppController::AppController(QObject* parent) : QObject(parent) {}

AppController::~AppController() = default;

void AppController::start() {
  showStartupWindow();
}

void AppController::openProject(const QString& path) {
  const QFileInfo pathInfo(path);
  if (pathInfo.isDir()) {
    const auto sidecar = ZoteroLoopSidecar::discover(pathInfo.absoluteFilePath());
    if (sidecar) {
      openZoteroLoopDirectory(pathInfo.absoluteFilePath(), *sidecar);
      return;
    }
  }

  // Check if this is a PDF file - if so, use the PDF import flow
  if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
    openPdfFile(path);
    return;
  }

  // Otherwise treat as a .ScanTailor project file
  MainWindow* window = createNewMainWindow();
  if (window) {
    window->openProject(path);
  }
}

void AppController::openZoteroLoopDirectory(const QString& directory, const ZoteroLoopSidecar& sidecar) {
  const QString projectFile = ProjectFolder::findProjectFile(directory);
  if (!projectFile.isEmpty()) {
    MainWindow* window = createNewMainWindow();
    if (window) {
      window->openProject(projectFile);
    }
    return;
  }

  openPdfFile(sidecar.sourcePdf, directory);
}

void AppController::openPdfFile(const QString& pdfFile, const QString& projectDirectory) {
  // Read PDF info to get page count and detected DPI
  const PdfReader::PdfInfo pdfInfo = PdfReader::readPdfInfo(pdfFile);
  if (pdfInfo.pageCount == 0) {
    QMessageBox::warning(nullptr, tr("Error"), PdfReadError::message(pdfFile));
    return;
  }

  const bool benchmarkAuto = qEnvironmentVariableIntValue("SCANTAILOR_BENCHMARK_AUTO") != 0;
  int selectedDpi = pdfInfo.detectedDpi;
  if (!benchmarkAuto) {
    // Show DPI selection dialog during normal interactive imports.
    PdfImportDialog dialog(nullptr, pdfFile, pdfInfo.pageCount, pdfInfo.detectedDpi);
    if (dialog.exec() != QDialog::Accepted) {
      return;  // User cancelled
    }
    selectedDpi = dialog.selectedDpi();
  }

  // Store the selected import DPI for this PDF
  PdfReader::setImportDpi(pdfFile, selectedDpi);

  // Create new MainWindow and import the PDF
  MainWindow* window = createNewMainWindow();
  if (window) {
    if (projectDirectory.isEmpty()) {
      window->importPdfFile(pdfFile);
    } else {
      window->importPdfFileToProject(pdfFile, projectDirectory);
    }
    if (benchmarkAuto) {
      QTimer::singleShot(0, window, &MainWindow::startBenchmarkAutoProcess);
    }
  }
}

void AppController::showStartupWindow() {
  if (!m_startupWindow) {
    m_startupWindow = new StartupWindow();
    connectStartupWindow();
  }

  // Center on screen
  m_startupWindow->show();
  m_startupWindow->raise();
  m_startupWindow->activateWindow();
}

MainWindow* AppController::createNewMainWindow() {
  // Hide startup window when we have a main window
  if (m_startupWindow) {
    m_startupWindow->hide();
  }

  // Find existing visible window to cascade from
  MainWindow* lastWindow = nullptr;
  for (int i = m_mainWindows.size() - 1; i >= 0; --i) {
    if (m_mainWindows[i] && m_mainWindows[i]->isVisible()) {
      lastWindow = m_mainWindows[i];
      break;
    }
  }

  // Create window - skip geometry restoration if we're going to cascade
  MainWindow* window = new MainWindow(lastWindow == nullptr);
  window->setAttribute(Qt::WA_DeleteOnClose, true);
  m_mainWindows.append(window);
  connectMainWindow(window);

  QSettings settings;
  if (settings.value("mainWindow/maximized", false).toBool()) {
    QTimer::singleShot(0, window, &QMainWindow::showMaximized);
  } else {
    if (lastWindow) {
      // Cascade: offset from the last active window
      const int cascade = 22;  // macOS title bar height
      QPoint newPos = lastWindow->pos() + QPoint(cascade, cascade);
      QSize newSize = lastWindow->size();

      // Check if window would go off screen
      QScreen* screen = QGuiApplication::primaryScreen();
      if (screen) {
        QRect screenGeom = screen->availableGeometry();
        if (newPos.x() + newSize.width() > screenGeom.right() ||
            newPos.y() + newSize.height() > screenGeom.bottom()) {
          newPos = screenGeom.topLeft() + QPoint(cascade, cascade);
        }
      }
      window->setGeometry(newPos.x(), newPos.y(), newSize.width(), newSize.height());
    }
    window->show();
  }
  window->raise();
  window->activateWindow();

  return window;
}

void AppController::connectStartupWindow() {
  connect(m_startupWindow, &StartupWindow::openProjectRequested, this, &AppController::onOpenProjectRequested);
  connect(m_startupWindow, &StartupWindow::importPdfRequested, this, &AppController::onImportPdfRequested);
  connect(m_startupWindow, &StartupWindow::importFolderRequested, this, &AppController::onImportFolderRequested);
  connect(m_startupWindow, &StartupWindow::recentProjectRequested, this, &AppController::onRecentProjectRequested);
}

void AppController::connectMainWindow(MainWindow* window) {
  connect(window, &MainWindow::projectClosed, this, &AppController::onMainWindowProjectClosed);
  connect(window, &MainWindow::newProjectRequested, this, &AppController::onNewProjectFromMainWindow);
  connect(window, &MainWindow::quitRequested, this, &AppController::onQuitRequested);
  connect(window, &QObject::destroyed, this, [this, window]() {
    removeMainWindow(window);
  });
}

void AppController::removeMainWindow(MainWindow* window) {
  m_mainWindows.removeAll(window);

  // If no more windows, either quit or show startup
  if (!hasActiveMainWindows()) {
    if (m_quitting) {
      QApplication::quit();
    } else {
      showStartupWindow();
    }
  }
}

bool AppController::hasActiveMainWindows() const {
  for (const auto& window : m_mainWindows) {
    if (window && window->isVisible()) {
      return true;
    }
  }
  return false;
}

void AppController::onOpenProjectRequested() {
  // Show file dialog with StartupWindow as parent (so it stays in front)
  const QString projectDir(QSettings().value("project/lastDir").toString());
  const QString projectFile(QFileDialog::getOpenFileName(
      m_startupWindow, tr("Open Project"), projectDir, tr("Scan Tailor Projects") + " (*.ScanTailor)"));

  if (projectFile.isEmpty()) {
    // User cancelled - stay on startup window
    return;
  }

  // User selected a file - create new MainWindow and open the project
  MainWindow* window = createNewMainWindow();
  if (window) {
    window->openProject(projectFile);
  }
}

void AppController::onImportPdfRequested() {
  // Show PDF file dialog with StartupWindow as parent
  QSettings settings;
  QString lastDir = settings.value("lastInputDir").toString();
  if (lastDir.isEmpty() || !QDir(lastDir).exists()) {
    lastDir = QDir::homePath();
  }

  const QString pdfFile(
      QFileDialog::getOpenFileName(m_startupWindow, tr("Import PDF"), lastDir, tr("PDF Files") + " (*.pdf *.PDF)"));

  if (pdfFile.isEmpty()) {
    // User cancelled
    return;
  }

  // Save directory for next time
  settings.setValue("lastInputDir", QFileInfo(pdfFile).absolutePath());

  // Read PDF info to get page count and detected DPI
  const PdfReader::PdfInfo pdfInfo = PdfReader::readPdfInfo(pdfFile);
  if (pdfInfo.pageCount == 0) {
    QMessageBox::warning(m_startupWindow, tr("Error"), PdfReadError::message(pdfFile));
    return;
  }

  // Show DPI selection dialog
  PdfImportDialog dialog(m_startupWindow, pdfFile, pdfInfo.pageCount, pdfInfo.detectedDpi);
  if (dialog.exec() != QDialog::Accepted) {
    return;  // User cancelled
  }

  // Store the selected import DPI for this PDF
  PdfReader::setImportDpi(pdfFile, dialog.selectedDpi());

  // Create new MainWindow and import the PDF
  MainWindow* window = createNewMainWindow();
  if (window) {
    window->importPdfFile(pdfFile);
  }
}

void AppController::onImportFolderRequested() {
  // Use ProjectCreationContext to handle the dialog flow (including fix DPI if needed)
  // It will delete itself when done
  auto* context = new ProjectCreationContext(m_startupWindow);
  connect(context, &ProjectCreationContext::done, this, &AppController::onProjectCreationDone);
}

void AppController::onProjectCreationDone(ProjectCreationContext* context) {
  if (context->files().empty()) {
    // User cancelled or no files selected
    return;
  }

  // Create new MainWindow and create the project
  MainWindow* window = createNewMainWindow();
  if (window) {
    window->createProjectFromFiles(QString(),  // inputDir not needed
                                   context->outDir(), context->files(),
                                   context->layoutDirection() == Qt::RightToLeft, false);
  }
}

void AppController::onRecentProjectRequested(const QString& path) {
  MainWindow* window = createNewMainWindow();
  if (window) {
    window->openProject(path);
  }
}

void AppController::onMainWindowProjectClosed() {
  MainWindow* window = qobject_cast<MainWindow*>(sender());
  if (window) {
    window->close();  // This will trigger destroyed signal and removeMainWindow
  }
}

void AppController::onNewProjectFromMainWindow() {
  // User clicked "New Project" from MainWindow - show startup window
  // Keep the existing window open with its current project
  showStartupWindow();
}

void AppController::onQuitRequested() {
  m_quitting = true;  // Mark that we're in a quit sequence

  // Schedule the quit chain to continue after the current window finishes closing.
  // This ensures we don't have issues with the sender window still being in the
  // widget list or event loop issues.
  QTimer::singleShot(0, this, [this]() {
    // Find the next visible MainWindow to close
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      MainWindow* window = qobject_cast<MainWindow*>(widget);
      if (window && window->isVisible()) {
        // Connect this window's quitRequested signal so the chain continues
        connect(window, &MainWindow::quitRequested, this, &AppController::onQuitRequested, Qt::UniqueConnection);
        window->quitApp();  // This will trigger its save prompt
        return;  // Wait for this window to finish before closing the next
      }
    }
    // No more visible MainWindows - quit the application
    QApplication::quit();
  });
}
