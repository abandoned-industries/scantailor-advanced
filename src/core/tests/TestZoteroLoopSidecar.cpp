// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <boost/test/unit_test.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "ZoteroLoopSidecar.h"
#include "filters/export/Settings.h"

namespace {
bool writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray validSidecar(const QString& sourcePdf) {
  return QStringLiteral(
             R"({"version":1,"pluginVersion":"0.1.2","futureInformation":{"safe":true},"itemKey":"ABCD1234","libraryID":1,"itemTitle":"Example Book","sourcePdf":"%1","returnUrl":"http://127.0.0.1:23119/st-spectre/return","token":"secret-token"})")
      .arg(sourcePdf)
      .toUtf8();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ZoteroLoopSidecarTest)

BOOST_AUTO_TEST_CASE(valid_sidecar_is_parsed) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QString sourcePdf = QDir(temp.path()).filePath("source.pdf");
  const QString sidecarPath = QDir(temp.path()).filePath(".zotero-loop.json");
  BOOST_REQUIRE(writeFile(sidecarPath, validSidecar(sourcePdf)));

  const auto sidecar = ZoteroLoopSidecar::fromFile(sidecarPath);
  BOOST_REQUIRE(sidecar.has_value());
  BOOST_CHECK_EQUAL(sidecar->version, 1);
  BOOST_CHECK_EQUAL(sidecar->pluginVersion.toStdString(), "0.1.2");
  BOOST_CHECK_EQUAL(sidecar->itemKey.toStdString(), "ABCD1234");
  BOOST_CHECK_EQUAL(sidecar->libraryId, 1);
  BOOST_CHECK_EQUAL(sidecar->itemTitle.toStdString(), "Example Book");
  BOOST_CHECK_EQUAL(sidecar->sourcePdf.toStdString(), sourcePdf.toStdString());
  BOOST_CHECK_EQUAL(sidecar->token.toStdString(), "secret-token");
}

BOOST_AUTO_TEST_CASE(malformed_sidecar_is_rejected) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QString sidecarPath = QDir(temp.path()).filePath(".zotero-loop.json");
  BOOST_REQUIRE(writeFile(sidecarPath, QByteArray("{not-json")));
  BOOST_CHECK(!ZoteroLoopSidecar::fromFile(sidecarPath).has_value());
}

BOOST_AUTO_TEST_CASE(sidecar_without_plugin_version_remains_a_valid_loop_project) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QString sourcePdf = QDir(temp.path()).filePath("source.pdf");
  QByteArray sidecarContents = validSidecar(sourcePdf);
  sidecarContents.replace(R"("pluginVersion":"0.1.2",)", "");
  const QString sidecarPath = QDir(temp.path()).filePath(".zotero-loop.json");
  BOOST_REQUIRE(writeFile(sidecarPath, sidecarContents));

  const auto sidecar = ZoteroLoopSidecar::fromFile(sidecarPath);
  BOOST_REQUIRE(sidecar.has_value());
  BOOST_CHECK(sidecar->pluginVersion.isEmpty());
}

BOOST_AUTO_TEST_CASE(plugin_versions_compare_by_numeric_segments) {
  BOOST_CHECK(ZoteroLoopSidecar::pluginVersionIsOlderThan("0.1.1", "0.1.2"));
  BOOST_CHECK(!ZoteroLoopSidecar::pluginVersionIsOlderThan("0.1.2", "0.1.2"));
  BOOST_CHECK(!ZoteroLoopSidecar::pluginVersionIsOlderThan("0.2", "0.1.2"));
  BOOST_CHECK(!ZoteroLoopSidecar::pluginVersionIsOlderThan("0.1.10", "0.1.2"));
  BOOST_CHECK(!ZoteroLoopSidecar::pluginVersionIsOlderThan("1.0.0", "0.1.2"));
  BOOST_CHECK(ZoteroLoopSidecar::pluginVersionIsOlderThan("", "0.1.2"));
  BOOST_CHECK(ZoteroLoopSidecar::pluginVersionIsOlderThan("unknown", "0.1.2"));
}

BOOST_AUTO_TEST_CASE(discovery_walks_up_from_nested_project_path) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  QDir root(temp.path());
  BOOST_REQUIRE(root.mkpath("nested/project"));
  const QString sourcePdf = root.filePath("source.pdf");
  const QString sidecarPath = root.filePath(".zotero-loop.json");
  BOOST_REQUIRE(writeFile(sidecarPath, validSidecar(sourcePdf)));

  const QString nestedProject = root.filePath("nested/project/book.ScanTailor");
  const auto sidecar = ZoteroLoopSidecar::discover(nestedProject);
  BOOST_REQUIRE(sidecar.has_value());
  BOOST_CHECK_EQUAL(sidecar->sidecarPath.toStdString(), sidecarPath.toStdString());
}

BOOST_AUTO_TEST_CASE(loop_default_does_not_override_an_explicit_project_choice) {
  export_::Settings settings;
  BOOST_CHECK(!settings.sendToZotero());
  BOOST_CHECK(!settings.hasExplicitSendToZoteroChoice());

  settings.armSendToZoteroForLoopProject();
  BOOST_CHECK(settings.sendToZotero());
  BOOST_CHECK(!settings.hasExplicitSendToZoteroChoice());

  settings.setSendToZotero(false, true);
  settings.armSendToZoteroForLoopProject();
  BOOST_CHECK(!settings.sendToZotero());
  BOOST_CHECK(settings.hasExplicitSendToZoteroChoice());

  settings.setSendToZotero(true, true);
  settings.armSendToZoteroForLoopProject();
  BOOST_CHECK(settings.sendToZotero());
  BOOST_CHECK(settings.hasExplicitSendToZoteroChoice());
}

BOOST_AUTO_TEST_SUITE_END()
