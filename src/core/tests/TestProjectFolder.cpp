// Copyright (C) 2024 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ProjectFolder.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

namespace Tests {
namespace {
bool writeFile(const QString& path, const QByteArray& contents) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  return file.write(contents) == contents.size();
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectFolderTestSuite)

BOOST_AUTO_TEST_CASE(copy_output_preserves_pages_and_cache) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  const QString sourceDir = QDir(temp.path()).filePath("temporary-output");
  const QString thumbsDir = QDir(sourceDir).filePath("cache/thumbs");
  BOOST_REQUIRE(QDir().mkpath(thumbsDir));
  BOOST_REQUIRE(writeFile(QDir(sourceDir).filePath("page0001.tif"), "page data"));
  BOOST_REQUIRE(writeFile(QDir(thumbsDir).filePath("page0001.png"), "thumbnail data"));

  ProjectFolder project(QDir(temp.path()).filePath("SavedProject"));
  BOOST_REQUIRE(project.create());
  BOOST_REQUIRE(project.copyOutputFrom(sourceDir));

  BOOST_CHECK(readFile(QDir(project.outputDir()).filePath("page0001.tif")) == QByteArray("page data"));
  BOOST_CHECK(readFile(QDir(project.outputDir()).filePath("cache/thumbs/page0001.png"))
              == QByteArray("thumbnail data"));

  BOOST_REQUIRE(writeFile(QDir(sourceDir).filePath("page0001.tif"), "updated page data"));
  BOOST_REQUIRE(project.copyOutputFrom(sourceDir));
  BOOST_CHECK(readFile(QDir(project.outputDir()).filePath("page0001.tif"))
              == QByteArray("updated page data"));
}

BOOST_AUTO_TEST_CASE(project_file_is_visible_even_with_trailing_directory_separator) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());

  const QString folderPath = QDir(temp.path()).filePath("Loop Project");
  BOOST_REQUIRE(QDir().mkpath(folderPath));
  const ProjectFolder project(folderPath + QDir::separator());

  BOOST_CHECK(project.projectFilePath() == QDir(folderPath).filePath("Loop Project.ScanTailor"));
}

BOOST_AUTO_TEST_CASE(find_project_file_preserves_legacy_and_prefers_visible_name) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QDir folder(temp.path());

  const QString hidden = folder.filePath(".ScanTailor");
  BOOST_REQUIRE(writeFile(hidden, "hidden legacy project"));
  BOOST_CHECK(ProjectFolder::findProjectFile(temp.path()) == hidden);
  BOOST_CHECK(ProjectFolder::isValidProjectFolder(temp.path()));

  const QString visible = folder.filePath(QFileInfo(temp.path()).fileName() + ".ScanTailor");
  BOOST_REQUIRE(writeFile(visible, "visible project"));
  BOOST_CHECK(ProjectFolder::findProjectFile(temp.path()) == visible);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
