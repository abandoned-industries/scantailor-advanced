// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_STARTUPWINDOW_H_
#define SCANTAILOR_APP_STARTUPWINDOW_H_

#include <QCloseEvent>
#include <QMenuBar>
#include <QString>
#include <QWidget>

class NewOpenProjectPanel;

class StartupWindow : public QWidget {
  Q_OBJECT

 public:
  explicit StartupWindow(QWidget* parent = nullptr);
  ~StartupWindow() override;

 signals:
  void openProjectRequested();
  void importPdfRequested();
  void importFolderRequested();
  void recentProjectRequested(const QString& path);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void showAboutDialog();
  void revealZoteroPlugin();

 private:
  NewOpenProjectPanel* m_panel;
#ifdef Q_OS_MAC
  QMenuBar* m_menuBar = nullptr;
#endif
};

#endif  // SCANTAILOR_APP_STARTUPWINDOW_H_
