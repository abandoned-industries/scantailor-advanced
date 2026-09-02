// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Fixture writer for the Stage 1.5.3 frozen v4 project fixtures.
//
// Usage: project_fixture_writer <output-dir>
//
// Writes two complete, self-contained project fixtures:
//   <output-dir>/image-project/   project.ScanTailor + scans/*.png (synthetic)
//   <output-dir>/pdf600-project/  project.ScanTailor + book-600dpi.pdf
//
// The committed fixtures under src/core/tests/fixtures/projects/ were produced
// by this program (see the README there). The fixtures are FROZEN: they pin
// the version-4 dialect as written on the freeze date. Regenerating them
// re-freezes to the current writer's output — do that only deliberately, and
// re-review the diff.
//
// All content is deterministic: fixed geometry, fixed synthetic images, and a
// programmatically assembled single-page PDF (144x216 pt = 2x3 in, so 1200x1800
// px at the 600-DPI import this fixture freezes).

#include <StageSequence.h>

#include <QApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "Dpi.h"
#include "ImageInfo.h"
#include "ImageMetadata.h"
#include "ProjectPages.h"
#include "RoundtripProjectBuilder.h"

using namespace Tests;

namespace {

// Deterministic little scan-like PNG (no timestamps in Qt's PNG output).
bool writeSyntheticPng(const QString& path, const QSize& size, bool spread) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(qRgb(245, 242, 235));
  QPainter painter(&image);
  painter.setPen(QPen(Qt::black, 3));
  const int columns = spread ? 2 : 1;
  for (int c = 0; c < columns; ++c) {
    const int x0 = size.width() * c / columns + size.width() / (10 * columns);
    const int x1 = size.width() * (c + 1) / columns - size.width() / (10 * columns);
    for (int i = 0; i < 12; ++i) {
      const int y = size.height() / 8 + i * size.height() / 16;
      painter.drawLine(x0, y, x1, y);
    }
  }
  painter.end();
  return image.save(path, "PNG");
}

// Minimal valid one-page PDF, 144x216 pt, with computed xref offsets.
bool writeMinimalPdf(const QString& path) {
  const QByteArray header = "%PDF-1.4\n";
  std::vector<QByteArray> objects;
  objects.push_back("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  objects.push_back("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
  objects.push_back(
      "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 144 216] "
      "/Contents 4 0 R /Resources << >> >>\nendobj\n");
  const QByteArray content
      = "0 0 0 RG 4 w\n20 20 104 176 re S\n0 0 0 rg\n30 100 84 12 re f\n40 140 64 8 re f\n";
  objects.push_back("4 0 obj\n<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content
                    + "endstream\nendobj\n");

  QByteArray pdf = header;
  std::vector<qsizetype> offsets;
  for (const QByteArray& object : objects) {
    offsets.push_back(pdf.size());
    pdf += object;
  }
  const qsizetype xrefOffset = pdf.size();
  pdf += "xref\n0 " + QByteArray::number(static_cast<int>(objects.size() + 1)) + "\n";
  pdf += "0000000000 65535 f \n";
  for (qsizetype offset : offsets) {
    pdf += QByteArray::number(static_cast<qulonglong>(offset)).rightJustified(10, '0') + " 00000 n \n";
  }
  pdf += "trailer\n<< /Size " + QByteArray::number(static_cast<int>(objects.size() + 1))
         + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(static_cast<qulonglong>(xrefOffset)) + "\n%%EOF\n";

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  return file.write(pdf) == pdf.size();
}

bool writeImageProjectFixture(const QDir& fixtureDir) {
  if (!fixtureDir.mkpath("scans") || !fixtureDir.mkpath("out")) {
    return false;
  }
  const QString scansDir = fixtureDir.filePath("scans");
  if (!writeSyntheticPng(scansDir + "/page1.png", QSize(1000, 1400), false)
      || !writeSyntheticPng(scansDir + "/spread.png", QSize(2000, 1400), true)) {
    return false;
  }

  ProjectModel model(scansDir);
  StageSequence stages(model.pages, makeNullAccessor());
  populateAllFilters(stages, model);
  const QString projectFile = fixtureDir.filePath("project.ScanTailor");
  const QString save0 = writeProjectXml(stages, model.pages, projectFile, fixtureDir.filePath("out"));
  if (save0.isEmpty()) {
    return false;
  }

  // fix_orientation has no public settings accessor: inject its non-default
  // rotation at the DOM level (dialect from Filter::writeParams) and rewrite.
  QDomDocument doc;
  if (!doc.setContent(save0)) {
    return false;
  }
  if (!injectFixOrientationRotation(doc, BuilderExpectations::fixOrientationDegrees)) {
    return false;
  }
  QFile out(projectFile);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const QByteArray bytes = doc.toByteArray(2);
  return out.write(bytes) == bytes.size();
}

bool writePdf600ProjectFixture(const QDir& fixtureDir) {
  if (!fixtureDir.mkpath(".") || !fixtureDir.mkpath("out")) {
    return false;
  }
  const QString pdfPath = fixtureDir.filePath("book-600dpi.pdf");
  if (!writeMinimalPdf(pdfPath)) {
    return false;
  }

  // One PDF page imported at 600 DPI: metadata carries the 600-DPI pixel size.
  const ImageId imageId(pdfPath, 1);  // 1-based page for multi-page sources
  const ImageMetadata metadata(QSize(Pdf600Expectations::metadataWidth, Pdf600Expectations::metadataHeight),
                               Dpi(Pdf600Expectations::importDpi, Pdf600Expectations::importDpi));
  const std::vector<ImageInfo> images{ImageInfo(imageId, metadata, 1, false, false)};
  auto pages = std::make_shared<ProjectPages>(images, Qt::LeftToRight);
  const PageId pageId(imageId, PageId::SINGLE_PAGE);

  StageSequence stages(pages, makeNullAccessor());
  populatePdf600Filters(stages, pageId);
  const QString save = writeProjectXml(stages, pages, fixtureDir.filePath("project.ScanTailor"),
                                       fixtureDir.filePath("out"));
  return !save.isEmpty();
}

}  // namespace

int main(int argc, char** argv) {
  setenv("QT_QPA_PLATFORM", "offscreen", /*overwrite=*/0);
  QApplication::setOrganizationName(QStringLiteral("ScanTailorSpectreTests"));
  QApplication::setApplicationName(QStringLiteral("project_fixture_writer"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QTemporaryDir settingsDir;
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
  QApplication app(argc, argv);

  if (argc != 2) {
    std::fprintf(stderr, "usage: %s OUTPUT_DIR\n", argv[0]);
    return 2;
  }
  const QDir outputDir(QString::fromLocal8Bit(argv[1]));
  if (!writeImageProjectFixture(QDir(outputDir.filePath("image-project")))) {
    std::fprintf(stderr, "failed to write image-project fixture\n");
    return 1;
  }
  if (!writePdf600ProjectFixture(QDir(outputDir.filePath("pdf600-project")))) {
    std::fprintf(stderr, "failed to write pdf600-project fixture\n");
    return 1;
  }
  std::printf("fixtures written under %s\n", argv[1]);
  return 0;
}
