// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StartupWindow.h"

#include <QApplication>
#include <QDialog>
#include <QMenuBar>
#include <QResource>
#include <QShortcut>
#include <QVBoxLayout>

#include "NewOpenProjectPanel.h"
#include "ZoteroPluginInstaller.h"
#include "ui_AboutDialog.h"
#include "version.h"

StartupWindow::StartupWindow(QWidget* parent) : QWidget(parent) {
  setWindowTitle(tr("ScanTailor Spectre"));
  setWindowFlags(Qt::Window);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_panel = new NewOpenProjectPanel(this);
  layout->addWidget(m_panel);

  // Forward signals from the panel
  connect(m_panel, &NewOpenProjectPanel::openProject, this, &StartupWindow::openProjectRequested);
  connect(m_panel, &NewOpenProjectPanel::importPdf, this, &StartupWindow::importPdfRequested);
  connect(m_panel, &NewOpenProjectPanel::importFolder, this, &StartupWindow::importFolderRequested);
  connect(m_panel, &NewOpenProjectPanel::openRecentProject, this, &StartupWindow::recentProjectRequested);

#ifdef Q_OS_MAC
  // Create a menu bar (nullptr makes it a global menu bar on macOS)
  m_menuBar = new QMenuBar(nullptr);

  // File menu
  auto* fileMenu = m_menuBar->addMenu(tr("&File"));

  auto* importPdfAction = fileMenu->addAction(tr("Import PDF..."));
  importPdfAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
  connect(importPdfAction, &QAction::triggered, this, &StartupWindow::importPdfRequested);

  auto* importFolderAction = fileMenu->addAction(tr("Import Folder..."));
  connect(importFolderAction, &QAction::triggered, this, &StartupWindow::importFolderRequested);

  fileMenu->addSeparator();

  auto* openProjectAction = fileMenu->addAction(tr("Open Project..."));
  openProjectAction->setShortcut(QKeySequence::Open);
  connect(openProjectAction, &QAction::triggered, this, &StartupWindow::openProjectRequested);

  fileMenu->addSeparator();

  auto* quitAction = fileMenu->addAction(tr("Quit"));
  quitAction->setShortcut(QKeySequence::Quit);
  quitAction->setMenuRole(QAction::QuitRole);
  // Routed through the controller so that any open project window gets closed
  // the normal way (save prompt included) before the application exits.
  connect(quitAction, &QAction::triggered, this, &StartupWindow::quitRequested);

  // Help menu
  auto* helpMenu = m_menuBar->addMenu(tr("&Help"));
  auto* installZoteroPluginAction = helpMenu->addAction(tr("Install Zotero Plugin…"));
  connect(installZoteroPluginAction, &QAction::triggered, this, &StartupWindow::revealZoteroPlugin);
  helpMenu->addSeparator();
  auto* aboutAction = helpMenu->addAction(tr("About ScanTailor Spectre"));
  aboutAction->setMenuRole(QAction::AboutRole);  // This moves it to the app menu on macOS
  connect(aboutAction, &QAction::triggered, this, &StartupWindow::showAboutDialog);
#endif

  // Set a reasonable size
  resize(500, 400);
}

StartupWindow::~StartupWindow() = default;

void StartupWindow::closeEvent(QCloseEvent* event) {
  // When the startup window is closed directly, quit the application. The
  // controller closes any remaining project window the normal way first.
  event->accept();
  emit quitRequested();
}

void StartupWindow::showAboutDialog() {
  Ui::AboutDialog ui;
  auto* dialog = new QDialog(this);
  ui.setupUi(dialog);
  ui.version->setText(QString(tr("version ")) + QString::fromUtf8(VERSION) +
                      QString(" (") + QString::fromUtf8(BUILD_TIMESTAMP) + QString(")"));

  QResource license(":/GPLv3.html");
  ui.licenseViewer->setHtml(QString::fromUtf8((const char*) license.data(), static_cast<int>(license.size())));

  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowModality(Qt::WindowModal);
  dialog->show();
}

void StartupWindow::revealZoteroPlugin() {
  ZoteroPluginInstaller::reveal(this);
}
