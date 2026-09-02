// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_BENCHMARKLOG_H_
#define SCANTAILOR_APP_BENCHMARKLOG_H_

#include <QJsonArray>
#include <QString>
#include <array>
#include <cstddef>

#include "ImageLoader.h"

class ProjectPages;
class StageSequence;

// Env-gated benchmark instrumentation for Auto Process runs, moved verbatim
// out of MainWindow. Write-only: nothing here reads window state back.
namespace benchmark {

bool benchmarkAutoEnabled();

bool benchmarkOcrEnabled();

bool useLegacyAutoProcessSequence();

struct BenchmarkOcrMetrics {
  qint64 processedPages = 0;
  qint64 blocks = 0;
  qint64 characters = 0;
  double meanConfidence = 0.0;
  QString textSha256;
  QJsonArray pages;
};

BenchmarkOcrMetrics collectBenchmarkOcrMetrics(StageSequence& stages, ProjectPages& pages);

void writeBenchmarkResult(bool completed,
                          qint64 totalMilliseconds,
                          size_t pageCount,
                          const std::array<qint64, 7>& stageMilliseconds,
                          const ImageLoader::Statistics& imageLoadStats,
                          const BenchmarkOcrMetrics& ocrMetrics);

}  // namespace benchmark

#endif  // SCANTAILOR_APP_BENCHMARKLOG_H_
