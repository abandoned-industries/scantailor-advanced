// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoteroPluginInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>

namespace ZoteroPluginInstaller {

void reveal(QWidget* parent) {
  const QString pluginPath = QDir::cleanPath(
      QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/st-spectre-loop.xpi"));
  if (!QFileInfo::exists(pluginPath)) {
    QMessageBox::warning(
        parent,
        QObject::tr("Zotero Plugin Not Found"),
        QObject::tr("The bundled Zotero plugin could not be found. Reinstall ScanTailor Spectre and try again."));
    return;
  }

  if (!QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                               {QStringLiteral("-R"), pluginPath})) {
    QMessageBox::warning(
        parent,
        QObject::tr("Could Not Reveal Zotero Plugin"),
        QObject::tr("ScanTailor Spectre could not reveal the bundled Zotero plugin in Finder."));
  }
}

}  // namespace ZoteroPluginInstaller
