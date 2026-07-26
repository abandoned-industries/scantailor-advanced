// Deterministic per-page evidence export for Finalize color detection.

#include <LeptonicaDetector.h>
#include <PdfReader.h>
#include <WhiteBalance.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QStringList>

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <vector>

namespace {

struct Options {
  QString inputPath;
  int dpi = PdfReader::DEFAULT_RENDER_DPI;
  int maxDimension = 0;
  int midtoneThreshold = 8;
  bool fullContent = false;
  bool allPages = false;
  std::vector<int> pages;
};

QRect deriveContentRect(const QImage& image) {
  const QColor background = WhiteBalance::estimateBackgroundColor(image);
  const int backgroundLuma = background.isValid()
                                 ? (background.red() + background.green() + background.blue()) / 3
                                 : 220;
  const int inkThreshold = std::clamp(backgroundLuma - 35, 70, 190);
  const int guardX = std::max(1, image.width() * 3 / 100);
  const int guardY = std::max(1, image.height() * 2 / 100);
  const int step = std::max(1, std::min(image.width(), image.height()) / 1200);

  int left = image.width();
  int top = image.height();
  int right = -1;
  int bottom = -1;
  const QImage rgbImage = image.convertToFormat(QImage::Format_RGB32);
  for (int y = guardY; y < image.height() - guardY; y += step) {
    const QRgb* line = reinterpret_cast<const QRgb*>(rgbImage.constScanLine(y));
    for (int x = guardX; x < image.width() - guardX; x += step) {
      const int luma = (qRed(line[x]) + qGreen(line[x]) + qBlue(line[x])) / 3;
      if (luma < inkThreshold) {
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
      }
    }
  }

  if (right < left || bottom < top) {
    return image.rect().adjusted(image.width() / 20, image.height() / 20,
                                 -image.width() / 20, -image.height() / 20);
  }
  const int padX = image.width() * 2 / 100;
  const int padY = image.height() * 2 / 100;
  return QRect(QPoint(left - padX, top - padY), QPoint(right + padX, bottom + padY))
      .intersected(image.rect());
}

void printHeader() {
  std::printf("page,dpi,source_width,source_height,content_x,content_y,content_width,content_height,"
              "detector_schema_version,requested_long_edge_cap,minimum_short_edge,scale_applied,"
              "analysis_width,analysis_height,analyzed_fraction,color_fraction,dark_ratio,midtone_ratio,"
              "light_ratio,cast_path,pre_cast_color_fraction,pre_cast_verdict,raw_verdict,"
              "midtone_threshold,cell_threshold,high_midtone_cell_count,"
              "tone_tiles,tone_regions,largest_region_tiles,tone_coverage,"
              "largest_region_coverage,largest_region_x,largest_region_y,"
              "largest_region_width,largest_region_height,"
              "document_prior,widespread_tone,reason");
  for (int gy = 1; gy <= 4; ++gy) {
    for (int gx = 1; gx <= 4; ++gx) {
      std::printf(",cell_%d_%d_x,cell_%d_%d_y,cell_%d_%d_width,cell_%d_%d_height,"
                  "cell_%d_%d_midtone_ratio,cell_%d_%d_sampled,cell_%d_%d_above_threshold",
                  gx, gy, gx, gy, gx, gy, gx, gy, gx, gy, gx, gy, gx, gy);
    }
  }
  for (int gy = 0; gy < 9; ++gy) {
    for (int gx = 0; gx < 9; ++gx) {
      std::printf(",tile_%d_%d_mid,tile_%d_%d_entropy,tile_%d_%d_blur_entropy,"
                  "tile_%d_%d_bins,tile_%d_%d_edge,tile_%d_%d_halo,"
                  "tile_%d_%d_variance,tile_%d_%d_candidate,tile_%d_%d_line_art",
                  gx, gy, gx, gy, gx, gy, gx, gy, gx, gy, gx, gy, gx, gy,
                  gx, gy, gx, gy);
    }
  }
  std::printf("\n");
}

bool printPage(const Options& options, const int pageIndex, const bool isPdf) {
  const QImage source = isPdf ? PdfReader::readImage(options.inputPath, pageIndex, options.dpi)
                              : QImage(options.inputPath);
  if (source.isNull()) {
    std::fprintf(stderr, "page %d: rasterization/load failed\n", pageIndex);
    return false;
  }

  const QRect contentBox =
      options.fullContent ? source.rect() : deriveContentRect(source);
  const QImage crop = source.copy(contentBox);

  LeptonicaDetector::DetectionEvidence evidence;
  LeptonicaDetector::AnalysisScalePolicy scalePolicy;
  scalePolicy.targetLongEdge = options.maxDimension;
  LeptonicaDetector::detectWithCastCompensation(
      crop, options.midtoneThreshold, &evidence, scalePolicy);

  std::printf("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%.6f,%s,%s,"
              "%d,%.6f,%d,%d,%d,%d,%.6f,%.6f,%d,%d,%d,%d,%d,%d,%s",
              pageIndex,
              options.dpi,
              source.width(),
              source.height(),
              contentBox.x(),
              contentBox.y(),
              contentBox.width(),
              contentBox.height(),
              evidence.detectorSchemaVersion,
              evidence.requestedLongEdgeCap,
              evidence.minimumShortEdge,
              evidence.scaleApplied ? 1 : 0,
              evidence.analysisSize.width(),
              evidence.analysisSize.height(),
              evidence.analyzedFraction,
              evidence.colorFraction,
              evidence.darkRatio,
              evidence.midtoneRatio,
              evidence.lightRatio,
              LeptonicaDetector::castPathToString(evidence.castPath),
              evidence.preCastColorFraction,
              LeptonicaDetector::colorTypeToString(evidence.preCastVerdict),
              LeptonicaDetector::colorTypeToString(evidence.rawVerdict),
              options.midtoneThreshold,
              evidence.cellThreshold,
              evidence.highMidtoneCellCount,
              evidence.continuousToneTileCount,
              evidence.continuousToneRegionCount,
              evidence.largestRegionTileCount,
              evidence.continuousToneCoverage,
              evidence.largestRegionCoverage,
              evidence.largestRegionBounds.x(),
              evidence.largestRegionBounds.y(),
              evidence.largestRegionBounds.width(),
              evidence.largestRegionBounds.height(),
              evidence.documentPrior ? 1 : 0,
              evidence.widespreadTone ? 1 : 0,
              evidence.reason.toUtf8().constData());
  for (const auto& cell : evidence.interiorCells) {
    std::printf(",%d,%d,%d,%d,%.6f,%d,%d",
                cell.x,
                cell.y,
                cell.width,
                cell.height,
                cell.midtoneRatio,
                cell.sampled ? 1 : 0,
                cell.aboveThreshold ? 1 : 0);
  }
  for (const auto& tile : evidence.toneTiles) {
    std::printf(",%.6f,%.6f,%.6f,%d,%.6f,%.6f,%.6f,%d,%d",
                tile.broadMidtoneRatio,
                tile.histogramEntropy,
                tile.blurredEntropy,
                tile.occupiedBins,
                tile.edgeDensity,
                tile.edgeHaloMidtoneRatio,
                tile.lowFrequencyVariance,
                tile.continuousTone ? 1 : 0,
                tile.lineArtRejected ? 1 : 0);
  }
  std::printf("\n");
  std::fflush(stdout);
  return true;
}

bool parseOptions(const QStringList& args, Options* options) {
  if (args.size() < 2) {
    return false;
  }
  options->inputPath = args[1];
  for (int i = 2; i < args.size(); ++i) {
    const QString& arg = args[i];
    if (arg == QStringLiteral("--all")) {
      options->allPages = true;
    } else if (arg == QStringLiteral("--full-content")) {
      options->fullContent = true;
    } else if (arg.startsWith(QStringLiteral("--dpi="))) {
      options->dpi = arg.mid(6).toInt();
    } else if (arg.startsWith(QStringLiteral("--max-analysis-dim="))) {
      options->maxDimension = arg.mid(19).toInt();
    } else if (arg.startsWith(QStringLiteral("--max-dim="))) {
      options->maxDimension = arg.mid(10).toInt();
    } else if (arg.startsWith(QStringLiteral("--midtone-threshold="))) {
      options->midtoneThreshold = arg.mid(20).toInt();
    } else {
      bool ok = false;
      const int page = arg.toInt(&ok);
      if (!ok || page < 0) {
        return false;
      }
      options->pages.push_back(page);
    }
  }
  return options->dpi > 0 && options->maxDimension >= 0;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");
  Options options;
  if (!parseOptions(app.arguments(), &options)) {
    std::fprintf(stderr,
                 "usage: %s INPUT [PAGE_INDEX...] [--all] [--dpi=N] [--max-dim=N] "
                 "[--max-analysis-dim=N] [--midtone-threshold=N] [--full-content]\n",
                 argv[0]);
    return 2;
  }

  const bool isPdf = PdfReader::canRead(options.inputPath);
  if (isPdf) {
    const PdfReader::PdfInfo info = PdfReader::readPdfInfo(options.inputPath);
    if (info.pageCount <= 0) {
      std::fprintf(stderr, "unable to read PDF metadata\n");
      return 1;
    }
    if (options.allPages) {
      options.pages.clear();
      options.pages.reserve(info.pageCount);
      for (int page = 0; page < info.pageCount; ++page) {
        options.pages.push_back(page);
      }
    }
  } else if (options.allPages || options.pages.empty()) {
    options.pages = {0};
  }

  if (options.pages.empty()) {
    std::fprintf(stderr, "specify at least one page index or --all\n");
    return 2;
  }

  printHeader();
  bool success = true;
  for (const int page : options.pages) {
    success = printPage(options, page, isPdf) && success;
  }
  return success ? 0 : 1;
}
