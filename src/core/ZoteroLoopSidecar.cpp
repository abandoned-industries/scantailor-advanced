// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoteroLoopSidecar.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {
const QString kSidecarName = QStringLiteral(".zotero-loop.json");
const QString kVersionOneReturnUrl = QStringLiteral("http://127.0.0.1:23119/st-spectre/return");

bool isInteger(const QJsonValue& value) {
  return value.isDouble() && std::isfinite(value.toDouble())
         && std::floor(value.toDouble()) == value.toDouble();
}

QString requiredString(const QJsonObject& object, const QString& key) {
  const QJsonValue value = object.value(key);
  return value.isString() ? value.toString().trimmed() : QString();
}

void logRejected(const QString& path, const QString& reason) {
  qWarning().noquote() << "Ignoring Zotero loop sidecar" << path << ":" << reason;
}
}  // namespace

std::optional<ZoteroLoopSidecar> ZoteroLoopSidecar::fromFile(const QString& sidecarPath) {
  QFile file(sidecarPath);
  if (!file.open(QIODevice::ReadOnly)) {
    logRejected(sidecarPath, QStringLiteral("could not be opened"));
    return std::nullopt;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    logRejected(sidecarPath, QStringLiteral("malformed JSON"));
    return std::nullopt;
  }

  const QJsonObject object = document.object();
  if (!isInteger(object.value(QStringLiteral("version")))
      || object.value(QStringLiteral("version")).toInt() != 1) {
    logRejected(sidecarPath, QStringLiteral("unsupported version"));
    return std::nullopt;
  }
  if (!isInteger(object.value(QStringLiteral("libraryID")))) {
    logRejected(sidecarPath, QStringLiteral("libraryID is not an integer"));
    return std::nullopt;
  }

  ZoteroLoopSidecar sidecar;
  sidecar.version = 1;
  sidecar.pluginVersion = requiredString(object, QStringLiteral("pluginVersion"));
  sidecar.libraryId = static_cast<qint64>(object.value(QStringLiteral("libraryID")).toDouble());
  sidecar.itemKey = requiredString(object, QStringLiteral("itemKey"));
  sidecar.itemTitle = requiredString(object, QStringLiteral("itemTitle"));
  sidecar.sourcePdf = requiredString(object, QStringLiteral("sourcePdf"));
  sidecar.returnUrl = requiredString(object, QStringLiteral("returnUrl"));
  sidecar.token = requiredString(object, QStringLiteral("token"));
  sidecar.sidecarPath = QFileInfo(sidecarPath).absoluteFilePath();

  if (sidecar.itemKey.isEmpty() || sidecar.itemTitle.isEmpty() || sidecar.sourcePdf.isEmpty()
      || sidecar.returnUrl.isEmpty() || sidecar.token.isEmpty()) {
    logRejected(sidecarPath, QStringLiteral("required string field is empty or has the wrong type"));
    return std::nullopt;
  }
  if (!QFileInfo(sidecar.sourcePdf).isAbsolute()) {
    logRejected(sidecarPath, QStringLiteral("sourcePdf is not absolute"));
    return std::nullopt;
  }
  if (sidecar.returnUrl != kVersionOneReturnUrl || !QUrl(sidecar.returnUrl).isValid()) {
    logRejected(sidecarPath, QStringLiteral("returnUrl does not match the version-1 contract"));
    return std::nullopt;
  }

  return sidecar;
}

bool ZoteroLoopSidecar::pluginVersionIsOlderThan(const QString& foundVersion,
                                                 const QString& minimumVersion) {
  const QStringList foundSegments = foundVersion.split(QLatin1Char('.'));
  const QStringList minimumSegments = minimumVersion.split(QLatin1Char('.'));
  if (foundSegments.isEmpty() || minimumSegments.isEmpty()) {
    return true;
  }

  const qsizetype segmentCount = std::max(foundSegments.size(), minimumSegments.size());
  for (qsizetype index = 0; index < segmentCount; ++index) {
    bool foundOk = true;
    bool minimumOk = true;
    const int found = index < foundSegments.size() ? foundSegments[index].toInt(&foundOk) : 0;
    const int minimum = index < minimumSegments.size() ? minimumSegments[index].toInt(&minimumOk) : 0;
    if (!foundOk || !minimumOk || found < 0 || minimum < 0) {
      return true;
    }
    if (found != minimum) {
      return found < minimum;
    }
  }
  return false;
}

std::optional<ZoteroLoopSidecar> ZoteroLoopSidecar::discover(const QString& projectFileOrDirectory) {
  const QFileInfo startInfo(projectFileOrDirectory);
  QDir directory(startInfo.isDir() ? startInfo.absoluteFilePath() : startInfo.absolutePath());

  while (true) {
    const QString sidecarPath = directory.filePath(kSidecarName);
    if (QFileInfo::exists(sidecarPath)) {
      return fromFile(sidecarPath);
    }

    const QString previousPath = directory.absolutePath();
    if (!directory.cdUp() || directory.absolutePath() == previousPath) {
      break;
    }
  }
  return std::nullopt;
}
