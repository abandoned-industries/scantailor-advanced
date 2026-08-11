// Copyright (C) 2024  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectFolder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

ProjectFolder::ProjectFolder(const QString& folderPath)
    : m_basePath(QDir::cleanPath(QFileInfo(folderPath).absoluteFilePath())) {}

QString ProjectFolder::projectFilePath() const {
  // Use folder name as project file name (e.g., "MyProject" folder -> "MyProject.ScanTailor")
  const QString folderName = QFileInfo(m_basePath).fileName();
  return QDir(m_basePath).filePath(folderName + ".ScanTailor");
}

QString ProjectFolder::originalsDir() const {
  return QDir(m_basePath).filePath("originals");
}

QString ProjectFolder::cacheDir() const {
  return QDir(m_basePath).filePath("cache");
}

QString ProjectFolder::outputDir() const {
  return QDir(m_basePath).filePath("output");
}

bool ProjectFolder::create() {
  QDir dir(m_basePath);

  // Create base directory if it doesn't exist
  if (!dir.exists() && !dir.mkpath(".")) {
    return false;
  }

  // Create subdirectories
  bool success = true;
  success = success && (dir.exists("originals") || dir.mkdir("originals"));
  success = success && (dir.exists("cache") || dir.mkdir("cache"));
  success = success && (dir.exists("output") || dir.mkdir("output"));

  return success;
}

QString ProjectFolder::copyOriginal(const QString& sourcePath) {
  QFileInfo sourceInfo(sourcePath);
  if (!sourceInfo.exists()) {
    return QString();
  }

  QString destPath = QDir(originalsDir()).filePath(sourceInfo.fileName());

  // Handle filename conflicts by adding numeric suffix
  if (QFile::exists(destPath)) {
    const QString baseName = sourceInfo.completeBaseName();
    const QString suffix = sourceInfo.suffix();
    int counter = 1;

    while (QFile::exists(destPath)) {
      QString newName;
      if (suffix.isEmpty()) {
        newName = QString("%1_%2").arg(baseName).arg(counter++);
      } else {
        newName = QString("%1_%2.%3").arg(baseName).arg(counter++).arg(suffix);
      }
      destPath = QDir(originalsDir()).filePath(newName);
    }
  }

  if (QFile::copy(sourcePath, destPath)) {
    return destPath;
  }

  return QString();
}

bool ProjectFolder::copyOutputFrom(const QString& sourceDir) {
  if (sourceDir.isEmpty() || !QDir(sourceDir).exists()) {
    return true;
  }

  const QString sourcePath = QDir(sourceDir).canonicalPath();
  const QString destinationPath = QDir(outputDir()).canonicalPath();
  if (!sourcePath.isEmpty() && sourcePath == destinationPath) {
    return true;
  }

  return copyDirectoryContents(sourceDir, outputDir());
}

bool ProjectFolder::copyDirectoryContents(const QString& sourceDir, const QString& destinationDir) {
  QDir destination(destinationDir);
  if (!destination.exists() && !destination.mkpath(".")) {
    return false;
  }

  const QDir source(sourceDir);
  const QFileInfoList entries = source.entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
  for (const QFileInfo& entry : entries) {
    const QString destinationPath = destination.filePath(entry.fileName());
    if (entry.isDir()) {
      if (!copyDirectoryContents(entry.absoluteFilePath(), destinationPath)) {
        return false;
      }
      continue;
    }

    if (QFile::exists(destinationPath) && !QFile::remove(destinationPath)) {
      return false;
    }
    if (!QFile::copy(entry.absoluteFilePath(), destinationPath)) {
      return false;
    }
  }
  return true;
}

bool ProjectFolder::isValidProjectFolder(const QString& path) {
  return !findProjectFile(path).isEmpty();
}

QString ProjectFolder::findProjectFile(const QString& path) {
  QDir dir(path);
  if (!dir.exists()) {
    return QString();
  }

  const QString folderName = QFileInfo(QDir::cleanPath(dir.absolutePath())).fileName();
  const QString visibleProject = dir.filePath(folderName + ".ScanTailor");
  if (QFileInfo(visibleProject).isFile()) {
    return visibleProject;
  }

  const QString legacyProject = dir.filePath(QStringLiteral("project.ScanTailor"));
  if (QFileInfo(legacyProject).isFile()) {
    return legacyProject;
  }

  const QString hiddenProject = dir.filePath(QStringLiteral(".ScanTailor"));
  if (QFileInfo(hiddenProject).isFile()) {
    return hiddenProject;
  }

  const QFileInfoList projects = dir.entryInfoList(
      QStringList{QStringLiteral("*.ScanTailor")}, QDir::Files | QDir::Readable | QDir::Hidden, QDir::Name);
  return projects.isEmpty() ? QString() : projects.constFirst().absoluteFilePath();
}
