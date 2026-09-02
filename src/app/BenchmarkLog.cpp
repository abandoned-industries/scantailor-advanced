// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BenchmarkLog.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <memory>

#include "PageSequence.h"
#include "ProjectPages.h"
#include "StageSequence.h"
#include "filters/ocr/Filter.h"
#include "filters/ocr/OcrResult.h"
#include "filters/ocr/Settings.h"

namespace benchmark {

bool benchmarkAutoEnabled() {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("SCANTAILOR_BENCHMARK_AUTO", &ok);
  return ok && value != 0;
}

bool benchmarkOcrEnabled() {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("SCANTAILOR_BENCHMARK_OCR", &ok);
  return ok && value != 0;
}

bool useLegacyAutoProcessSequence() {
  bool ok = false;
  const int value = qEnvironmentVariableIntValue("SCANTAILOR_LEGACY_AUTO_PROCESS_SEQUENCE", &ok);
  return ok && value != 0;
}

BenchmarkOcrMetrics collectBenchmarkOcrMetrics(StageSequence& stages, ProjectPages& projectPages) {
  BenchmarkOcrMetrics ocrMetrics;
  QCryptographicHash textHash(QCryptographicHash::Sha256);
  QByteArray recognizedText;
  double confidenceTotal = 0.0;
  const auto ocrSettings = stages.ocrFilter()->settings();
  const PageSequence pages = projectPages.toPageSequence(PAGE_VIEW);
  for (size_t pageIndex = 0; pageIndex < pages.numPages(); ++pageIndex) {
    const PageInfo& page = pages.pageAt(pageIndex);
    const std::unique_ptr<ocr::OcrResult> result = ocrSettings->getOcrResult(page.id());
    if (!result) {
      continue;
    }
    ++ocrMetrics.processedPages;
    QCryptographicHash pageTextHash(QCryptographicHash::Sha256);
    qint64 pageBlocks = 0;
    qint64 pageCharacters = 0;
    qint64 lowConfidenceBlocks = 0;
    double pageConfidenceTotal = 0.0;
    double minimumConfidence = 1.0;
    double coveredArea = 0.0;
    textHash.addData(QByteArrayView("\x1e", 1));
    recognizedText += "\n\fPAGE ";
    recognizedText += QByteArray::number(pageIndex + 1);
    recognizedText += "\n";
    for (const ocr::OcrWord& word : result->words()) {
      ++pageBlocks;
      pageCharacters += word.text.size();
      pageConfidenceTotal += word.confidence;
      minimumConfidence = std::min(minimumConfidence, static_cast<double>(word.confidence));
      lowConfidenceBlocks += word.confidence < 0.5f;
      coveredArea += word.boundingBox.width() * word.boundingBox.height();
      pageTextHash.addData(word.text.toUtf8());
      pageTextHash.addData(QByteArrayView("\x1f", 1));
      ++ocrMetrics.blocks;
      ocrMetrics.characters += word.text.size();
      confidenceTotal += word.confidence;
      textHash.addData(word.text.toUtf8());
      textHash.addData(QByteArrayView("\x1f", 1));
      recognizedText += word.text.toUtf8();
      recognizedText += '\n';
    }
    const double imageArea =
        static_cast<double>(result->imageWidth()) * result->imageHeight();
    ocrMetrics.pages.append(QJsonObject{
        {QStringLiteral("page"), static_cast<qint64>(pageIndex + 1)},
        {QStringLiteral("blocks"), pageBlocks},
        {QStringLiteral("characters"), pageCharacters},
        {QStringLiteral("mean_confidence"),
         pageBlocks > 0 ? pageConfidenceTotal / pageBlocks : 0.0},
        {QStringLiteral("minimum_confidence"),
         pageBlocks > 0 ? minimumConfidence : 0.0},
        {QStringLiteral("low_confidence_blocks"), lowConfidenceBlocks},
        {QStringLiteral("covered_area_fraction"),
         imageArea > 0.0 ? coveredArea / imageArea : 0.0},
        {QStringLiteral("text_sha256"),
         QString::fromLatin1(pageTextHash.result().toHex())}
    });
  }
  if (ocrMetrics.blocks > 0) {
    ocrMetrics.meanConfidence = confidenceTotal / static_cast<double>(ocrMetrics.blocks);
  }
  ocrMetrics.textSha256 = QString::fromLatin1(textHash.result().toHex());

  const QString textPath = qEnvironmentVariable("SCANTAILOR_BENCHMARK_OCR_TEXT_PATH");
  if (!textPath.isEmpty()) {
    QSaveFile textFile(textPath);
    if (!textFile.open(QIODevice::WriteOnly)
        || textFile.write(recognizedText) != recognizedText.size()
        || !textFile.commit()) {
      qWarning() << "Failed to write benchmark OCR text:" << textPath << textFile.errorString();
    }
  }
  return ocrMetrics;
}

void writeBenchmarkResult(const bool completed,
                          const qint64 totalMilliseconds,
                          const size_t pageCount,
                          const std::array<qint64, 7>& stageMilliseconds,
                          const ImageLoader::Statistics& imageLoadStats,
                          const BenchmarkOcrMetrics& ocrMetrics) {
  const QString resultPath = qEnvironmentVariable("SCANTAILOR_BENCHMARK_RESULT_PATH");
  if (resultPath.isEmpty()) {
    return;
  }

  QJsonObject stages{
      {QStringLiteral("split"), stageMilliseconds[0]},
      {QStringLiteral("deskew"), stageMilliseconds[1]},
      {QStringLiteral("page_box"), stageMilliseconds[2]},
      {QStringLiteral("select_content"), stageMilliseconds[3]},
      {QStringLiteral("page_layout"), stageMilliseconds[4]},
      {QStringLiteral("output"), stageMilliseconds[5]},
      {QStringLiteral("ocr"), stageMilliseconds[6]}
  };
  QJsonObject imageLoads{
      {QStringLiteral("cache_hits"), static_cast<qint64>(imageLoadStats.cacheHits)},
      {QStringLiteral("unique_misses"), static_cast<qint64>(imageLoadStats.cacheMisses)},
      {QStringLiteral("leader_decodes"), static_cast<qint64>(imageLoadStats.leaderDecodes)},
      {QStringLiteral("coalesced_waiters"), static_cast<qint64>(imageLoadStats.coalescedWaiters)},
      {QStringLiteral("pdf_rasterizations"), static_cast<qint64>(imageLoadStats.pdfRasterizations)},
      {QStringLiteral("decoded_bytes"), static_cast<qint64>(imageLoadStats.decodedBytes)}
  };
  QJsonObject ocr{
      {QStringLiteral("processed_pages"), ocrMetrics.processedPages},
      {QStringLiteral("blocks"), ocrMetrics.blocks},
      {QStringLiteral("characters"), ocrMetrics.characters},
      {QStringLiteral("mean_confidence"), ocrMetrics.meanConfidence},
      {QStringLiteral("text_sha256"), ocrMetrics.textSha256}
  };
  if (!ocrMetrics.pages.isEmpty()) {
    ocr.insert(QStringLiteral("pages"), ocrMetrics.pages);
  }
  const QJsonObject result{
      {QStringLiteral("completed"), completed},
      {QStringLiteral("total_ms"), totalMilliseconds},
      {QStringLiteral("page_count"), static_cast<qint64>(pageCount)},
      {QStringLiteral("stages_ms"), stages},
      {QStringLiteral("image_loads"), imageLoads},
      {QStringLiteral("ocr"), ocr},
      {QStringLiteral("legacy_sequence"), useLegacyAutoProcessSequence()}
  };

  QSaveFile file(resultPath);
  if (!file.open(QIODevice::WriteOnly)
      || file.write(QJsonDocument(result).toJson(QJsonDocument::Compact)) < 0
      || !file.commit()) {
    qWarning() << "Failed to write benchmark result:" << resultPath << file.errorString();
  }
}

}  // namespace benchmark
