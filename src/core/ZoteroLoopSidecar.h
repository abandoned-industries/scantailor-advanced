// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ZOTEROLOOPSIDECAR_H_
#define SCANTAILOR_CORE_ZOTEROLOOPSIDECAR_H_

#include <QString>

#include <optional>

struct ZoteroLoopSidecar {
  static constexpr const char* MINIMUM_SUPPORTED_PLUGIN_VERSION = "0.1.2";

  int version = 0;
  QString pluginVersion;
  QString itemKey;
  qint64 libraryId = 0;
  QString itemTitle;
  QString sourcePdf;
  QString returnUrl;
  QString token;
  QString sidecarPath;

  static std::optional<ZoteroLoopSidecar> fromFile(const QString& sidecarPath);
  static std::optional<ZoteroLoopSidecar> discover(const QString& projectFileOrDirectory);
  static bool pluginVersionIsOlderThan(const QString& foundVersion, const QString& minimumVersion);
};

#endif  // SCANTAILOR_CORE_ZOTEROLOOPSIDECAR_H_
