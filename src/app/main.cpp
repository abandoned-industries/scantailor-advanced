// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <config.h>
#include <core/Application.h>
#include <core/ApplicationSettings.h>
#include <core/ColorSchemeFactory.h>
#include <core/ColorSchemeManager.h>
#include <core/FontIconPack.h>
#include <core/IconProvider.h>
#include <core/StyledIconPack.h>

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include "AccessibilitySafeguard.h"
#include "AppController.h"
#include "MainWindow.h"

#ifdef Q_OS_MAC
#include "MetalLifecycle.h"
#endif

int main(int argc, char* argv[]) {
  // Force OpenGL backend for Qt Quick so QWebEngineView's internal QQuickWidget
  // composites correctly. Without this, QWebEngineView renders as a black rectangle
  // on macOS because the default Metal backend conflicts with WebEngine's OpenGL
  // compositor.
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

  // Qt6 enables high DPI scaling by default
  Application app(argc, argv);

  // Qt 6.11's macOS accessibility bridge crashes when an external AX client
  // reads AXSelectedChildren from any item view. Must run before widgets exist.
  installAccessibilitySafeguard();

#ifdef Q_OS_MAC
  // Initialize Metal lifecycle observer to detect app backgrounding
  // This must be done after QApplication is created but before any Metal operations
  metalLifecycleInit();

  // macOS style: don't quit when last window is closed, only on Cmd+Q or Quit menu
  Application::setQuitOnLastWindowClosed(false);
#endif

#ifdef _WIN32
  // Get rid of all references to Qt's installation directory.
  Application::setLibraryPaths(QStringList(Application::applicationDirPath()));
#endif

  QStringList args = Application::arguments();

  // This information is used by QSettings.
  Application::setApplicationName(APPLICATION_NAME);
  Application::setOrganizationName(ORGANIZATION_NAME);

  QSettings::setDefaultFormat(QSettings::IniFormat);
  if (app.isPortableVersion()) {
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app.getPortableConfigPath());
  }

  app.installLanguage(ApplicationSettings::getInstance().getLanguage());

  {
    std::unique_ptr<ColorScheme> scheme
        = ColorSchemeFactory().create(ApplicationSettings::getInstance().getColorScheme());
    ColorSchemeManager::instance().setColorScheme(*scheme);
  }
  IconProvider::getInstance().setIconPack(StyledIconPack::createDefault());

#ifdef Q_OS_MAC
  // macOS: Use AppController to manage StartupWindow and MainWindow
  AppController controller;

  // Connect file open events (from Finder double-click, drag-drop, Open With).
  // Queued: the event is delivered from inside AppKit's Apple Event handler, and
  // the import flow is modal - running it there wedges the application (no window
  // is ordered front and no input is processed until the handler returns).
  QObject::connect(&app, &Application::fileOpenRequested, &controller, &AppController::openProject,
                   Qt::QueuedConnection);

  // Quit requests from the system (Dock menu, "quit" Apple Event, log out).
  QObject::connect(&app, &Application::quitRequested, &controller, &AppController::quitApplication,
                   Qt::QueuedConnection);

  if (args.size() > 1) {
    // Deferred for the same reason: the import flow is modal and must not run
    // before the event loop is up.
    const QString startupPath = args.at(1);
    QTimer::singleShot(0, &controller, [&controller, startupPath]() { controller.openProject(startupPath); });
  } else {
    controller.start();
  }
#else
  // Other platforms: Show MainWindow directly (original behavior)
  auto* mainWnd = new MainWindow();
  mainWnd->setAttribute(Qt::WA_DeleteOnClose);
  QSettings settings;
  if (settings.value("mainWindow/maximized", false).toBool()) {
    QTimer::singleShot(0, mainWnd, &QMainWindow::showMaximized);
  } else {
    mainWnd->show();
  }
  if (args.size() > 1) {
    mainWnd->openProject(args.at(1));
  }
#endif

  return Application::exec();
}  // main
