// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PnmMetadataLoader.h"

#include <QIODevice>

#include "ImageMetadata.h"

namespace {
// Reject obviously absurd dimensions.
constexpr qint64 kMaxDimension = 1000000;

// The dimensions must appear within this many bytes of the file start.
// Real headers are a couple dozen bytes; the allowance covers comments.
constexpr int kHeaderWindow = 1024;

bool isPnmWhitespace(char c) {
  return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\v') || (c == '\f');
}

/**
 * Advances \p pos past whitespace and '#' comments (which run to the end of
 * the line).  Returns false if the window is exhausted first.
 */
bool skipWhitespaceAndComments(const char* data, qint64 size, qint64& pos) {
  while (pos < size) {
    if (isPnmWhitespace(data[pos])) {
      ++pos;
    } else if (data[pos] == '#') {
      while ((pos < size) && (data[pos] != '\n')) {
        ++pos;
      }
    } else {
      return true;
    }
  }
  return false;
}

/**
 * Parses one unsigned decimal number at \p pos.  Returns false when there is
 * no digit at \p pos or the value exceeds kMaxDimension.
 */
bool parseNumber(const char* data, qint64 size, qint64& pos, qint64& value) {
  if ((pos >= size) || (data[pos] < '0') || (data[pos] > '9')) {
    return false;
  }
  value = 0;
  while ((pos < size) && (data[pos] >= '0') && (data[pos] <= '9')) {
    value = value * 10 + (data[pos] - '0');
    if (value > kMaxDimension) {
      return false;
    }
    ++pos;
  }
  return true;
}
}  // namespace

ImageMetadataLoader::Status PnmMetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                            const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  // Everything is peeked, so the device is left untouched throughout.
  // The magic is 'P' plus a format digit plus a separator; requiring the
  // separator avoids claiming arbitrary text files starting with "P3x".
  char header[kHeaderWindow];
  const qint64 peeked = ioDevice.peek(header, sizeof(header));
  if (peeked < 3) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if ((header[0] != 'P') || (header[1] < '1') || (header[1] > '6')) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if (!isPnmWhitespace(header[2]) && (header[2] != '#')) {
    return FORMAT_NOT_RECOGNIZED;
  }

  // The magic matched; from here on a malformed header is a GENERIC_ERROR.
  qint64 pos = 2;
  qint64 width = 0;
  qint64 height = 0;
  if (!skipWhitespaceAndComments(header, peeked, pos) || !parseNumber(header, peeked, pos, width)
      || !skipWhitespaceAndComments(header, peeked, pos) || !parseNumber(header, peeked, pos, height)) {
    return GENERIC_ERROR;
  }
  if ((width <= 0) || (height <= 0)) {
    return GENERIC_ERROR;
  }

  // No physical resolution in PNM.
  out(ImageMetadata(QSize(int(width), int(height)), Dpi()));
  return LOADED;
}  // PnmMetadataLoader::loadMetadata
