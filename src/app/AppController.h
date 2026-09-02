// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_APPCONTROLLER_H_
#define SCANTAILOR_APP_APPCONTROLLER_H_

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class StartupWindow;
class MainWindow;
class ProjectCreationContext;
struct ZoteroLoopSidecar;

class AppController : public QObject {
  Q_OBJECT

 public:
  explicit AppController(QObject* parent = nullptr);
  ~AppController() override;

  void start();
  void openProject(const QString& path);
  void openPdfFile(const QString& pdfFile, const QString& projectDirectory = QString());

 public slots:
  void showStartupWindow();
  void quitApplication();

 private slots:
  void onOpenProjectRequested();
  void onImportPdfRequested();
  void onImportFolderRequested();
  void onRecentProjectRequested(const QString& path);
  void onMainWindowProjectClosed();
  void onNewProjectFromMainWindow();
  void onProjectCreationDone(ProjectCreationContext* context);
  void onQuitAborted();

 private:
  MainWindow* createNewMainWindow();
  void ensureWindowVisible();
  void connectStartupWindow();
  void connectMainWindow(MainWindow* window);
  void removeMainWindow(MainWindow* window);
  bool hasActiveMainWindows() const;
  void openZoteroLoopDirectory(const QString& directory, const ZoteroLoopSidecar& sidecar);

  QPointer<StartupWindow> m_startupWindow;
  QList<QPointer<MainWindow>> m_mainWindows;
  bool m_quitting = false;
};

#endif  // SCANTAILOR_APP_APPCONTROLLER_H_
