// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Jp2MetadataLoader.h"

#include <QIODevice>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Dpm.h"
#include "ImageMetadata.h"

namespace {
// Box types (big-endian FourCCs).
constexpr quint32 kBoxJp2h = 0x6A703268;  // 'jp2h' - JP2 header superbox
constexpr quint32 kBoxJp2c = 0x6A703263;  // 'jp2c' - contiguous codestream
constexpr quint32 kBoxIhdr = 0x69686472;  // 'ihdr' - image header
constexpr quint32 kBoxRes = 0x72657320;   // 'res ' - resolution superbox
constexpr quint32 kBoxResc = 0x72657363;  // 'resc' - capture resolution
constexpr quint32 kBoxResd = 0x72657364;  // 'resd' - default display resolution

// Reject obviously absurd ihdr dimensions.
constexpr quint32 kMaxDimension = 1000000;

quint32 readBE32(const unsigned char* p) {
  return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 readBE16(const unsigned char* p) {
  return quint16((quint16(p[0]) << 8) | quint16(p[1]));
}

bool readExact(QIODevice& ioDevice, unsigned char* buf, qint64 numBytes) {
  while (numBytes > 0) {
    const qint64 read = ioDevice.read((char*) buf, numBytes);
    if (read <= 0) {
      return false;
    }
    buf += read;
    numBytes -= read;
  }
  return true;
}

bool skipBytes(QIODevice& ioDevice, qint64 numBytes) {
  unsigned char buf[4096];
  while (numBytes > 0) {
    const qint64 chunk = std::min<qint64>(numBytes, sizeof(buf));
    const qint64 read = ioDevice.read((char*) buf, chunk);
    if (read <= 0) {
      return false;
    }
    numBytes -= read;
  }
  return true;
}

/**
 * Reads one box header.  On success \p type receives the box type,
 * \p contentLength the number of content bytes that follow (-1 meaning
 * "extends to the end of the stream") and \p headerLength the number of
 * bytes consumed (8, or 16 for boxes with an extended length field).
 */
bool readBoxHeader(QIODevice& ioDevice, quint32& type, qint64& contentLength, qint64& headerLength) {
  unsigned char buf[8];
  if (!readExact(ioDevice, buf, 8)) {
    return false;
  }
  const quint32 lbox = readBE32(buf);
  type = readBE32(buf + 4);
  if (lbox == 0) {
    // Box extends to the end of the stream.
    contentLength = -1;
    headerLength = 8;
  } else if (lbox == 1) {
    // 64-bit extended length follows.
    unsigned char xlbuf[8];
    if (!readExact(ioDevice, xlbuf, 8)) {
      return false;
    }
    const quint64 xlbox = (quint64(readBE32(xlbuf)) << 32) | readBE32(xlbuf + 4);
    if (xlbox < 16 || xlbox > quint64(std::numeric_limits<qint64>::max())) {
      return false;
    }
    contentLength = qint64(xlbox) - 16;
    headerLength = 16;
  } else {
    if (lbox < 8) {
      return false;
    }
    contentLength = qint64(lbox) - 8;
    headerLength = 8;
  }
  return true;
}

/**
 * Parses the 10-byte payload of a 'resc' or 'resd' box.  Both store the
 * vertical and horizontal resolution as rationals scaled by a power of ten,
 * in grid points per meter.  Returns false for degenerate values.
 */
bool parseResolutionBox(const unsigned char* p, int& horizontalDpm, int& verticalDpm) {
  const quint16 vNum = readBE16(p);
  const quint16 vDenom = readBE16(p + 2);
  const quint16 hNum = readBE16(p + 4);
  const quint16 hDenom = readBE16(p + 6);
  const auto vExp = (signed char) p[8];
  const auto hExp = (signed char) p[9];
  if ((vDenom == 0) || (hDenom == 0) || (vNum == 0) || (hNum == 0)) {
    return false;
  }

  const double vPerMeter = double(vNum) / double(vDenom) * std::pow(10.0, double(vExp));
  const double hPerMeter = double(hNum) / double(hDenom) * std::pow(10.0, double(hExp));
  if (!(vPerMeter >= 1.0) || !(hPerMeter >= 1.0) || (vPerMeter > 1e9) || (hPerMeter > 1e9)) {
    return false;
  }

  horizontalDpm = int(std::lround(hPerMeter));
  verticalDpm = int(std::lround(vPerMeter));
  return true;
}

/**
 * Parses the sub-boxes of a 'res ' superbox, looking for a capture ('resc')
 * or default display ('resd') resolution.  Capture resolution wins, as it
 * describes the scan itself.  A malformed resolution box just leaves \p dpi
 * untouched (the box is optional).  Returns the number of bytes consumed,
 * or -1 on a read failure.
 */
qint64 parseResSuperbox(QIODevice& ioDevice, qint64 contentLength, Dpi& dpi, bool& haveCaptureRes) {
  qint64 remaining = contentLength;
  while (remaining >= 8) {
    quint32 type = 0;
    qint64 subContent = 0;
    qint64 subHeader = 0;
    if (!readBoxHeader(ioDevice, type, subContent, subHeader)) {
      return -1;
    }
    remaining -= subHeader;
    if (remaining < 0) {
      // Extended-length header straddling the superbox end.
      return -1;
    }
    if ((subContent < 0) || (subContent > remaining)) {
      // Malformed sub-box; the caller skips the rest of the superbox.
      return contentLength - remaining;
    }

    if (((type == kBoxResc) || (type == kBoxResd)) && (subContent >= 10)) {
      unsigned char buf[10];
      if (!readExact(ioDevice, buf, 10)) {
        return -1;
      }
      if (!skipBytes(ioDevice, subContent - 10)) {
        return -1;
      }
      int hDpm = 0;
      int vDpm = 0;
      if (parseResolutionBox(buf, hDpm, vDpm)) {
        if (type == kBoxResc) {
          dpi = Dpm(hDpm, vDpm);
          haveCaptureRes = true;
        } else if (!haveCaptureRes) {
          dpi = Dpm(hDpm, vDpm);
        }
      }
    } else if (!skipBytes(ioDevice, subContent)) {
      return -1;
    }
    remaining -= subContent;
  }
  return contentLength - remaining;
}
}  // namespace

ImageMetadataLoader::Status Jp2MetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                            const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  // The 12-byte JP2 signature box, followed by the file type box whose
  // major brand must be 'jp2 ' (this rejects .jpx and raw codestreams).
  // Everything is peeked, so the device is left untouched on a mismatch.
  static const unsigned char jp2Signature[12]
      = {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};

  unsigned char header[24];
  if (ioDevice.peek((char*) header, sizeof(header)) != qint64(sizeof(header))) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if (std::memcmp(header, jp2Signature, sizeof(jp2Signature)) != 0) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if ((std::memcmp(header + 16, "ftyp", 4) != 0) || (std::memcmp(header + 20, "jp2 ", 4) != 0)) {
    return FORMAT_NOT_RECOGNIZED;
  }

  // The signature matched.  From here on the device may be consumed and any
  // malformed structure is a GENERIC_ERROR, like in the other loaders.
  if (!skipBytes(ioDevice, sizeof(jp2Signature))) {
    return GENERIC_ERROR;
  }

  // Scan top-level boxes for the JP2 header superbox.  The spec requires it
  // to precede the codestream, so stopping at 'jp2c' bounds the scan.
  qint64 jp2hLength = -1;
  for (;;) {
    quint32 type = 0;
    qint64 contentLength = 0;
    qint64 headerLength = 0;
    if (!readBoxHeader(ioDevice, type, contentLength, headerLength)) {
      return GENERIC_ERROR;
    }
    if (type == kBoxJp2h) {
      if (contentLength < 8) {
        return GENERIC_ERROR;
      }
      jp2hLength = contentLength;
      break;
    }
    if ((type == kBoxJp2c) || (contentLength < 0)) {
      // Reached the codestream (or a box extending to the end of the file)
      // without seeing a JP2 header box.
      return GENERIC_ERROR;
    }
    if (!skipBytes(ioDevice, contentLength)) {
      return GENERIC_ERROR;
    }
  }

  // Walk the jp2h sub-boxes: 'ihdr' for the dimensions and optionally
  // 'res ' for the resolution.  No resolution box means undefined DPI,
  // which routes the user through the Fix DPI dialog.
  QSize size;
  Dpi dpi;
  bool haveIhdr = false;
  bool haveCaptureRes = false;

  qint64 remaining = jp2hLength;
  while (remaining >= 8) {
    quint32 type = 0;
    qint64 contentLength = 0;
    qint64 headerLength = 0;
    if (!readBoxHeader(ioDevice, type, contentLength, headerLength)) {
      return GENERIC_ERROR;
    }
    remaining -= headerLength;
    if ((contentLength < 0) || (contentLength > remaining)) {
      return GENERIC_ERROR;
    }

    if ((type == kBoxIhdr) && (contentLength >= 8)) {
      unsigned char buf[8];
      if (!readExact(ioDevice, buf, 8)) {
        return GENERIC_ERROR;
      }
      if (!skipBytes(ioDevice, contentLength - 8)) {
        return GENERIC_ERROR;
      }
      const quint32 height = readBE32(buf);
      const quint32 width = readBE32(buf + 4);
      if ((width == 0) || (height == 0) || (width > kMaxDimension) || (height > kMaxDimension)) {
        return GENERIC_ERROR;
      }
      size = QSize(int(width), int(height));
      haveIhdr = true;
    } else if (type == kBoxRes) {
      const qint64 consumed = parseResSuperbox(ioDevice, contentLength, dpi, haveCaptureRes);
      if ((consumed < 0) || !skipBytes(ioDevice, contentLength - consumed)) {
        return GENERIC_ERROR;
      }
    } else if (!skipBytes(ioDevice, contentLength)) {
      return GENERIC_ERROR;
    }
    remaining -= contentLength;
  }

  if (!haveIhdr) {
    return GENERIC_ERROR;
  }

  out(ImageMetadata(size, dpi));
  return LOADED;
}  // Jp2MetadataLoader::loadMetadata
