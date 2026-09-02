// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Writes the two frozen synthetic source pages for the OutputGenerator golden
// tests (Stage 1.5.1). Usage: golden_fixture_writer <fixtures/golden dir>.
// The pages are pure pixel arithmetic with fixed seeds — see
// GoldenSourcePages.cpp. Committed once; tests load the frozen PNGs.

#include <QCoreApplication>
#include <QDir>
#include <cstdio>

#include "GoldenSourcePages.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s FIXTURES_GOLDEN_DIR\n", argv[0]);
    return 2;
  }
  const QDir dir(QString::fromLocal8Bit(argv[1]));
  if (!dir.mkpath(".")) {
    return 1;
  }
  if (!Tests::makeGoldenTextPage().save(dir.filePath("text-page.png"), "PNG")
      || !Tests::makeGoldenMixedPage().save(dir.filePath("mixed-page.png"), "PNG")) {
    std::fprintf(stderr, "failed to write source pages\n");
    return 1;
  }
  std::printf("source pages written under %s\n", argv[1]);
  return 0;
}
