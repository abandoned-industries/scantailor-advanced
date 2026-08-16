// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageLoader.h>
#include <PdfReader.h>

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSemaphore>
#include <QString>
#include <QTemporaryDir>
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {
bool writeTestPdf(const QString& path, const QSizeF& pageSize, const QString& title) {
  QPdfWriter writer(path);
  writer.setResolution(72);
  writer.setPageSize(QPageSize(pageSize, QPageSize::Point));
  writer.setPageMargins(QMarginsF(), QPageLayout::Point);
  writer.setTitle(title);

  QPainter painter(&writer);
  if (!painter.isActive()) {
    return false;
  }
  painter.fillRect(QRectF(QPointF(), pageSize), Qt::white);
  painter.fillRect(QRectF(10, 10, pageSize.width() / 2, pageSize.height() / 2), Qt::black);
  return painter.end();
}

BOOST_AUTO_TEST_SUITE(PdfReaderTestSuite)

BOOST_AUTO_TEST_CASE(pdfHeaderMayAppearWithinFirst1024Bytes) {
  struct TestCase {
    QByteArray data;
    bool expected;
  };

  const std::vector<TestCase> testCases{
      {QByteArray::fromHex("efbbbf") + "%PDF-1.4\n", true},
      {"%PDF-1.4\n", true},
      {"not a PDF", false},
      {QByteArray(1024, 'x') + "%PDF-1.4\n", false},
  };

  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());

  for (std::size_t i = 0; i < testCases.size(); ++i) {
    QByteArray data = testCases[i].data;
    QBuffer buffer(&data);
    BOOST_REQUIRE(buffer.open(QIODevice::ReadOnly));
    const qint64 originalPosition = std::min<qint64>(2, data.size());
    BOOST_REQUIRE(buffer.seek(originalPosition));
    BOOST_CHECK_EQUAL(PdfReader::canRead(buffer), testCases[i].expected);
    BOOST_CHECK_EQUAL(buffer.pos(), originalPosition);

    const QString path = tempDir.filePath(QStringLiteral("header-%1.pdf").arg(i));
    QFile file(path);
    BOOST_REQUIRE(file.open(QIODevice::WriteOnly));
    BOOST_REQUIRE_EQUAL(file.write(data), data.size());
    file.close();
    BOOST_CHECK_EQUAL(PdfReader::canRead(path), testCases[i].expected);
  }
}

BOOST_AUTO_TEST_CASE(newlyStagedPdfIsRetriedUntilCopyCompletes) {
  const QString sourcePath = QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/ScanTailor Spectre Readme.pdf");
  QFile source(sourcePath);
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray sourceData = source.readAll();
  BOOST_REQUIRE(sourceData.startsWith("%PDF-"));
  BOOST_REQUIRE(sourceData.size() > 4096);

  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString stagedPath = tempDir.filePath("staging.pdf");
  QSemaphore headerWritten;
  bool writerOk = false;
  std::thread writer([&] {
    QFile staged(stagedPath);
    if (!staged.open(QIODevice::WriteOnly)
        || staged.write(sourceData.constData(), 1024) != 1024
        || !staged.flush()) {
      headerWritten.release();
      return;
    }
    headerWritten.release();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    writerOk = staged.write(sourceData.constData() + 1024, sourceData.size() - 1024)
        == sourceData.size() - 1024;
    staged.close();
  });

  headerWritten.acquire();
  const PdfReader::PdfInfo info = PdfReader::readPdfInfo(stagedPath);
  writer.join();
  BOOST_REQUIRE(writerOk);
  BOOST_CHECK_GT(info.pageCount, 0);
}

BOOST_AUTO_TEST_CASE(concurrentRasterizationProducesIdenticalImages) {
  const QString pdfPath = QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/ScanTailor Spectre Readme.pdf");
  BOOST_REQUIRE(QFileInfo::exists(pdfPath));

  constexpr int workerCount = 4;
  QSemaphore ready;
  QSemaphore start;
  std::vector<QImage> images(workerCount);
  std::vector<std::thread> workers;
  workers.reserve(workerCount);

  for (int i = 0; i < workerCount; ++i) {
    workers.emplace_back([&, i] {
      ready.release();
      start.acquire();
      images[i] = PdfReader::readImage(pdfPath, 0, 72);
    });
  }
  ready.acquire(workerCount);
  start.release(workerCount);
  for (std::thread& worker : workers) {
    worker.join();
  }

  BOOST_REQUIRE(!images.front().isNull());
  for (int i = 1; i < workerCount; ++i) {
    BOOST_REQUIRE(images[i].size() == images.front().size());
    BOOST_REQUIRE(images[i].format() == images.front().format());
    BOOST_REQUIRE_EQUAL(images[i].sizeInBytes(), images.front().sizeInBytes());
    BOOST_CHECK_EQUAL(
        std::memcmp(images[i].constBits(), images.front().constBits(), images.front().sizeInBytes()), 0);
  }
}

BOOST_AUTO_TEST_CASE(samePathReplacementReloadsDocument) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString path = tempDir.filePath("replace.pdf");

  BOOST_REQUIRE(writeTestPdf(path, QSizeF(100, 120), QStringLiteral("first")));
  const QImage first = PdfReader::readImage(path, 0, 72);
  BOOST_REQUIRE(!first.isNull());
  const QFileInfo firstInfo(path);
  const qint64 firstSize = firstInfo.size();
  const qint64 firstModificationTime = firstInfo.lastModified().toMSecsSinceEpoch();

  BOOST_REQUIRE(QFile::remove(path));
  BOOST_REQUIRE(writeTestPdf(path, QSizeF(220, 140), QString(200, QLatin1Char('x'))));
  const QFileInfo secondInfo(path);
  BOOST_REQUIRE(firstSize != secondInfo.size()
                || firstModificationTime != secondInfo.lastModified().toMSecsSinceEpoch());

  const QImage second = PdfReader::readImage(path, 0, 72);
  BOOST_REQUIRE(!second.isNull());
  BOOST_CHECK(first.size() != second.size());
}

BOOST_AUTO_TEST_CASE(decodedPdfCacheSeparatesRenderDpi) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString path = tempDir.filePath("dpi.pdf");
  BOOST_REQUIRE(writeTestPdf(path, QSizeF(100, 120), QStringLiteral("dpi")));

  PdfReader::setImportDpi(path, 72);
  const QImage lowDpi = ImageLoader::load(path, 0);
  PdfReader::setImportDpi(path, 144);
  const QImage highDpi = ImageLoader::load(path, 0);

  BOOST_REQUIRE(!lowDpi.isNull());
  BOOST_REQUIRE(!highDpi.isNull());
  BOOST_CHECK_EQUAL(highDpi.width(), lowDpi.width() * 2);
  BOOST_CHECK_EQUAL(highDpi.height(), lowDpi.height() * 2);
}

BOOST_AUTO_TEST_CASE(decodedImageCacheReportsHitsAndMisses) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString path = tempDir.filePath("stats.pdf");
  BOOST_REQUIRE(writeTestPdf(path, QSizeF(100, 120), QStringLiteral("stats")));

  PdfReader::setImportDpi(path, 72);
  ImageLoader::resetStatistics();
  const QImage first = ImageLoader::load(path, 0);
  const QImage second = ImageLoader::load(path, 0);
  const ImageLoader::Statistics stats = ImageLoader::statistics();

  BOOST_REQUIRE(!first.isNull());
  BOOST_REQUIRE(!second.isNull());
  BOOST_CHECK_EQUAL(stats.cacheMisses, 1);
  BOOST_CHECK_EQUAL(stats.cacheHits, 1);
  BOOST_CHECK_EQUAL(stats.leaderDecodes, 1);
  BOOST_CHECK_EQUAL(stats.pdfRasterizations, 1);
  BOOST_CHECK_EQUAL(stats.coalescedWaiters, 0);
  BOOST_CHECK_EQUAL(stats.decodedBytes, first.sizeInBytes());
}

BOOST_AUTO_TEST_CASE(concurrentDecodedImageLoadsShareOnePdfRasterization) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString path = tempDir.filePath("single-flight.pdf");
  BOOST_REQUIRE(writeTestPdf(path, QSizeF(612, 792), QStringLiteral("single-flight")));

  // A full letter page at 600 DPI keeps the elected leader busy long enough
  // for every simultaneously released worker to join the same cache flight.
  PdfReader::setImportDpi(path, 600);
  ImageLoader::resetStatistics();

  constexpr int workerCount = 8;
  QSemaphore ready;
  QSemaphore start;
  std::vector<QImage> images(workerCount);
  std::vector<std::thread> workers;
  workers.reserve(workerCount);
  for (int i = 0; i < workerCount; ++i) {
    workers.emplace_back([&, i] {
      ready.release();
      start.acquire();
      images[i] = ImageLoader::load(path, 0);
    });
  }
  ready.acquire(workerCount);
  start.release(workerCount);
  for (std::thread& worker : workers) {
    worker.join();
  }

  const ImageLoader::Statistics stats = ImageLoader::statistics();
  BOOST_REQUIRE(!images.front().isNull());
  for (int i = 1; i < workerCount; ++i) {
    BOOST_REQUIRE(images[i].constBits() == images.front().constBits());
  }
  BOOST_CHECK_EQUAL(stats.cacheHits, 0);
  BOOST_CHECK_EQUAL(stats.cacheMisses, 1);
  BOOST_CHECK_EQUAL(stats.leaderDecodes, 1);
  BOOST_CHECK_EQUAL(stats.coalescedWaiters, workerCount - 1);
  BOOST_CHECK_EQUAL(stats.pdfRasterizations, 1);
  BOOST_CHECK_EQUAL(stats.decodedBytes, images.front().sizeInBytes());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
