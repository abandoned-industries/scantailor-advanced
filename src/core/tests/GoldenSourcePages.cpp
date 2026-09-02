// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "GoldenSourcePages.h"

#include <algorithm>
#include <cstdint>

namespace Tests {
namespace {

constexpr int kWidth = 600;
constexpr int kHeight = 800;

struct Lcg {
  std::uint32_t state;
  explicit Lcg(std::uint32_t seed) : state(seed) {}
  std::uint32_t next() {
    state = state * 1664525u + 1013904223u;
    return state;
  }
  // Uniform in [0, n)
  int below(int n) { return static_cast<int>((next() >> 8) % static_cast<std::uint32_t>(n)); }
};

inline int clamp255(int v) {
  return std::clamp(v, 0, 255);
}

QImage makePaper() {
  QImage image(kWidth, kHeight, QImage::Format_RGB32);
  for (int y = 0; y < kHeight; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < kWidth; ++x) {
      // Slight warm tint drifting across x, cool drift down y.
      int r = 246 - (x * 10) / kWidth;
      int g = 243 - (x * 6) / kWidth - (y * 4) / kHeight;
      int b = 234 + (x * 4) / kWidth;
      // Illumination falloff toward the bottom-left corner.
      const int falloff = ((kWidth - x) + y) * 30 / (kWidth + kHeight);
      r -= falloff;
      g -= falloff;
      b -= falloff;
      line[x] = qRgb(clamp255(r), clamp255(g), clamp255(b));
    }
  }
  return image;
}

void drawInkRect(QImage& image, int x0, int y0, int w, int h, int ink) {
  const int x1 = std::min(x0 + w, kWidth);
  const int y1 = std::min(y0 + h, kHeight);
  for (int y = std::max(0, y0); y < y1; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = std::max(0, x0); x < x1; ++x) {
      line[x] = qRgb(ink, ink, ink + 4);
    }
  }
}

void drawTextLines(QImage& image, Lcg& rng) {
  for (int lineNo = 0; lineNo < 14; ++lineNo) {
    const int y0 = 84 + lineNo * 46;
    int x = 70 + (lineNo % 3) * 6;
    const int lineEnd = 530 - (lineNo % 4) * 18;
    while (x < lineEnd) {
      const int wordWidth = 18 + rng.below(46);
      const int inkShade = 12 + rng.below(30);
      drawInkRect(image, x, y0, std::min(wordWidth, lineEnd - x), 14, inkShade);
      // Word gap.
      x += wordWidth + 10 + rng.below(12);
    }
  }
}

void drawPhotoBlock(QImage& image, Lcg& rng) {
  // Dark frame.
  const int fx0 = 314, fy0 = 454, fx1 = 566, fy1 = 766;
  for (int y = fy0; y < fy1; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = fx0; x < fx1; ++x) {
      const bool border = (x < fx0 + 6) || (x >= fx1 - 6) || (y < fy0 + 6) || (y >= fy1 - 6);
      if (border) {
        line[x] = qRgb(38, 34, 30);
      }
    }
  }
  // Continuous-tone interior: two smooth gradients plus mild noise.
  const int px0 = fx0 + 6, py0 = fy0 + 6, px1 = fx1 - 6, py1 = fy1 - 6;
  for (int y = py0; y < py1; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = px0; x < px1; ++x) {
      const int u = (x - px0) * 255 / (px1 - px0);
      const int v = (y - py0) * 255 / (py1 - py0);
      const int noise = rng.below(25) - 12;
      const int r = clamp255(60 + (u * 2) / 3 + noise);
      const int g = clamp255(90 + v / 2 - u / 4 + noise);
      const int b = clamp255(150 - v / 3 + u / 5 + noise);
      line[x] = qRgb(r, g, b);
    }
  }
}

}  // namespace

QImage makeGoldenTextPage() {
  QImage image = makePaper();
  Lcg rng(0x5EED1234u);
  drawTextLines(image, rng);
  return image;
}

QImage makeGoldenMixedPage() {
  QImage image = makePaper();
  Lcg rng(0x5EED1234u);
  drawTextLines(image, rng);
  // Clear the text under the photo area back to paper-ish, then draw photo.
  for (int y = 448; y < kHeight; ++y) {
    QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 306; x < kWidth; ++x) {
      const int falloff = ((kWidth - x) + y) * 30 / (kWidth + kHeight);
      line[x] = qRgb(clamp255(246 - (x * 10) / kWidth - falloff),
                     clamp255(243 - (x * 6) / kWidth - (y * 4) / kHeight - falloff),
                     clamp255(234 + (x * 4) / kWidth - falloff));
    }
  }
  Lcg photoRng(0xC0FFEE42u);
  drawPhotoBlock(image, photoRng);
  return image;
}

}  // namespace Tests
