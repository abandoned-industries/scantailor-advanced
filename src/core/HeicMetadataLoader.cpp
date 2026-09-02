// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "HeicMetadataLoader.h"

#include <QByteArray>
#include <QIODevice>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "ImageMetadata.h"

namespace {
// Box types (big-endian FourCCs).
constexpr quint32 kBoxFtyp = 0x66747970;  // 'ftyp'
constexpr quint32 kBoxMeta = 0x6D657461;  // 'meta'
constexpr quint32 kBoxPitm = 0x7069746D;  // 'pitm' - primary item
constexpr quint32 kBoxIprp = 0x69707270;  // 'iprp' - item properties superbox
constexpr quint32 kBoxIpco = 0x6970636F;  // 'ipco' - item property container
constexpr quint32 kBoxIpma = 0x69706D61;  // 'ipma' - item property association
constexpr quint32 kBoxIspe = 0x69737065;  // 'ispe' - image spatial extents
constexpr quint32 kBoxClap = 0x636C6170;  // 'clap' - clean aperture

// Reject obviously absurd dimensions.
constexpr qint64 kMaxDimension = 1000000;

// The 'ftyp' box is peeked whole for the brand check; real ones are a few
// dozen bytes.
constexpr int kMaxFtypLength = 256;

// Upper bound on the buffered 'meta' box; iPhone photos carry a few KB.
constexpr qint64 kMaxMetaLength = 4 * 1024 * 1024;

// Bounded scan for the top-level 'meta' box.
constexpr int kMaxTopLevelBoxes = 64;

quint32 readBE32(const unsigned char* p) {
  return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 readBE16(const unsigned char* p) {
  return quint16((quint16(p[0]) << 8) | quint16(p[1]));
}

bool isHeifBrand(const unsigned char* p) {
  static const char* const kBrands[] = {"heic", "heix", "mif1", "msf1", "heif"};
  for (const char* brand : kBrands) {
    if (std::memcmp(p, brand, 4) == 0) {
      return true;
    }
  }
  return false;
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

/**
 * Reads one box header from the stream, as in Jp2MetadataLoader:
 * \p contentLength of -1 means "extends to the end of the stream".
 */
bool readBoxHeader(QIODevice& ioDevice, quint32& type, qint64& contentLength, qint64& headerLength) {
  unsigned char buf[8];
  if (!readExact(ioDevice, buf, 8)) {
    return false;
  }
  const quint32 lbox = readBE32(buf);
  type = readBE32(buf + 4);
  if (lbox == 0) {
    contentLength = -1;
    headerLength = 8;
  } else if (lbox == 1) {
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

/** A box located within the in-memory 'meta' buffer. */
struct BoxSpan {
  quint32 type = 0;
  const unsigned char* content = nullptr;
  qint64 length = 0;
};

/**
 * Iterates the boxes packed in [p, p + length).  Handles 32-bit and
 * extended 64-bit box lengths; a zero length means "to the end of the
 * span".  Malformed boxes end the iteration.
 */
class BoxIterator {
 public:
  BoxIterator(const unsigned char* p, qint64 length) : m_p(p), m_remaining(length) {}

  bool next(BoxSpan& box) {
    if (m_remaining < 8) {
      return false;
    }
    const quint32 lbox = readBE32(m_p);
    box.type = readBE32(m_p + 4);
    qint64 headerLength = 8;
    qint64 totalLength = 0;
    if (lbox == 0) {
      totalLength = m_remaining;
    } else if (lbox == 1) {
      if (m_remaining < 16) {
        return false;
      }
      const quint64 xlbox = (quint64(readBE32(m_p + 8)) << 32) | readBE32(m_p + 12);
      if ((xlbox < 16) || (xlbox > quint64(m_remaining))) {
        return false;
      }
      headerLength = 16;
      totalLength = qint64(xlbox);
    } else {
      if ((lbox < 8) || (qint64(lbox) > m_remaining)) {
        return false;
      }
      totalLength = qint64(lbox);
    }
    if (totalLength < headerLength) {
      return false;
    }
    box.content = m_p + headerLength;
    box.length = totalLength - headerLength;
    m_p += totalLength;
    m_remaining -= totalLength;
    return true;
  }

 private:
  const unsigned char* m_p;
  qint64 m_remaining;
};

/** Parses an 'ispe' full box: 4 bytes version/flags, then width and height. */
bool parseIspe(const BoxSpan& box, qint64& width, qint64& height) {
  if (box.length < 12) {
    return false;
  }
  width = readBE32(box.content + 4);
  height = readBE32(box.content + 8);
  return (width > 0) && (height > 0) && (width <= kMaxDimension) && (height <= kMaxDimension);
}

/**
 * Parses a 'clap' box (8 signed 32-bit rationals) into the clean-aperture
 * width and height.  Returns false for degenerate values.
 */
bool parseClap(const BoxSpan& box, qint64& width, qint64& height) {
  if (box.length < 32) {
    return false;
  }
  const auto widthN = qint32(readBE32(box.content));
  const auto widthD = qint32(readBE32(box.content + 4));
  const auto heightN = qint32(readBE32(box.content + 8));
  const auto heightD = qint32(readBE32(box.content + 12));
  if ((widthD <= 0) || (heightD <= 0) || (widthN <= 0) || (heightN <= 0)) {
    return false;
  }
  width = qint64(std::llround(double(widthN) / double(widthD)));
  height = qint64(std::llround(double(heightN) / double(heightD)));
  return (width > 0) && (height > 0);
}

/**
 * Extracts from an 'ipma' box the 1-based ipco property indices associated
 * with \p itemId.  Returns false when the box is malformed or the item has
 * no entry.
 */
bool findItemProperties(const BoxSpan& ipma, quint32 itemId, std::vector<quint32>& indices) {
  const unsigned char* p = ipma.content;
  qint64 remaining = ipma.length;
  if (remaining < 8) {
    return false;
  }
  const unsigned char version = p[0];
  const quint32 flags = readBE32(p) & 0xFFFFFF;
  const quint32 entryCount = readBE32(p + 4);
  p += 8;
  remaining -= 8;

  for (quint32 i = 0; i < entryCount; ++i) {
    quint32 entryItemId = 0;
    if (version < 1) {
      if (remaining < 2) {
        return false;
      }
      entryItemId = readBE16(p);
      p += 2;
      remaining -= 2;
    } else {
      if (remaining < 4) {
        return false;
      }
      entryItemId = readBE32(p);
      p += 4;
      remaining -= 4;
    }
    if (remaining < 1) {
      return false;
    }
    const int associationCount = p[0];
    p += 1;
    remaining -= 1;

    const int associationLength = (flags & 1) ? 2 : 1;
    if (remaining < qint64(associationCount) * associationLength) {
      return false;
    }
    for (int j = 0; j < associationCount; ++j) {
      quint32 index = 0;
      if (flags & 1) {
        index = readBE16(p) & 0x7FFF;
      } else {
        index = p[0] & 0x7F;
      }
      p += associationLength;
      remaining -= associationLength;
      if (entryItemId == itemId) {
        indices.push_back(index);
      }
    }
    if (entryItemId == itemId) {
      return true;
    }
  }
  return false;
}
}  // namespace

ImageMetadataLoader::Status HeicMetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                             const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  // The file must start with an 'ftyp' box carrying a HEIF brand.
  // Everything is peeked, so the device is left untouched on a mismatch.
  unsigned char ftypHeader[kMaxFtypLength];
  if (ioDevice.peek((char*) ftypHeader, 16) != 16) {
    return FORMAT_NOT_RECOGNIZED;
  }
  const quint32 ftypLength = readBE32(ftypHeader);
  if ((readBE32(ftypHeader + 4) != kBoxFtyp) || (ftypLength < 16) || (ftypLength > kMaxFtypLength)
      || (ftypLength % 4 != 0)) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if (ioDevice.peek((char*) ftypHeader, ftypLength) != qint64(ftypLength)) {
    return FORMAT_NOT_RECOGNIZED;
  }
  // Major brand at offset 8, compatible brands from offset 16.
  bool brandMatched = isHeifBrand(ftypHeader + 8);
  for (quint32 offset = 16; !brandMatched && (offset + 4 <= ftypLength); offset += 4) {
    brandMatched = isHeifBrand(ftypHeader + offset);
  }
  if (!brandMatched) {
    // Some other ISO BMFF file (mp4, avif, ...).
    return FORMAT_NOT_RECOGNIZED;
  }

  // The brand matched.  From here on the device may be consumed and any
  // malformed structure is a GENERIC_ERROR, like in the other loaders.
  if (!skipBytes(ioDevice, ftypLength)) {
    return GENERIC_ERROR;
  }

  // Scan top-level boxes for 'meta' and buffer its content for in-memory
  // parsing ('ipco' precedes 'ipma' but must be looked up afterwards).
  QByteArray metaBuffer;
  for (int i = 0; i < kMaxTopLevelBoxes; ++i) {
    quint32 type = 0;
    qint64 contentLength = 0;
    qint64 headerLength = 0;
    if (!readBoxHeader(ioDevice, type, contentLength, headerLength)) {
      return GENERIC_ERROR;
    }
    if (type == kBoxMeta) {
      if ((contentLength < 4) || (contentLength > kMaxMetaLength)) {
        return GENERIC_ERROR;
      }
      metaBuffer.resize(int(contentLength));
      if (!readExact(ioDevice, (unsigned char*) metaBuffer.data(), contentLength)) {
        return GENERIC_ERROR;
      }
      break;
    }
    if ((contentLength < 0) || !skipBytes(ioDevice, contentLength)) {
      // A box extending to the end of the file, or a read failure, without
      // having seen 'meta'.
      return GENERIC_ERROR;
    }
  }
  if (metaBuffer.isEmpty()) {
    return GENERIC_ERROR;
  }

  // 'meta' is a full box: skip 4 bytes of version/flags, then walk its
  // children for 'pitm' (primary item id) and 'iprp'/'ipco'/'ipma'.
  const auto* metaContent = (const unsigned char*) metaBuffer.constData() + 4;
  const qint64 metaLength = metaBuffer.size() - 4;

  bool havePrimaryItemId = false;
  quint32 primaryItemId = 0;
  std::vector<BoxSpan> properties;  // ipco children, in order (1-based index).
  BoxSpan ipmaBox;

  BoxIterator metaIt(metaContent, metaLength);
  BoxSpan box;
  while (metaIt.next(box)) {
    if ((box.type == kBoxPitm) && (box.length >= 6)) {
      const unsigned char version = box.content[0];
      if (version < 1) {
        primaryItemId = readBE16(box.content + 4);
        havePrimaryItemId = true;
      } else if (box.length >= 8) {
        primaryItemId = readBE32(box.content + 4);
        havePrimaryItemId = true;
      }
    } else if (box.type == kBoxIprp) {
      BoxIterator iprpIt(box.content, box.length);
      BoxSpan child;
      while (iprpIt.next(child)) {
        if (child.type == kBoxIpco) {
          BoxIterator ipcoIt(child.content, child.length);
          BoxSpan property;
          while (ipcoIt.next(property)) {
            properties.push_back(property);
          }
        } else if (child.type == kBoxIpma) {
          ipmaBox = child;
        }
      }
    }
  }

  // Primary path: the 'ispe' (and optional 'clap') associated with the
  // primary item via 'ipma'.  Fallback: the largest 'ispe' in 'ipco'
  // (thumbnails are smaller than the primary image).
  qint64 width = 0;
  qint64 height = 0;
  bool haveExtents = false;
  qint64 clapWidth = 0;
  qint64 clapHeight = 0;
  bool haveClap = false;

  if (havePrimaryItemId && (ipmaBox.content != nullptr)) {
    std::vector<quint32> indices;
    if (findItemProperties(ipmaBox, primaryItemId, indices)) {
      for (const quint32 index : indices) {
        if ((index < 1) || (index > properties.size())) {
          continue;
        }
        const BoxSpan& property = properties[index - 1];
        if ((property.type == kBoxIspe) && !haveExtents) {
          haveExtents = parseIspe(property, width, height);
        } else if ((property.type == kBoxClap) && !haveClap) {
          haveClap = parseClap(property, clapWidth, clapHeight);
        }
      }
    }
  }
  if (!haveExtents) {
    haveClap = false;
    for (const BoxSpan& property : properties) {
      qint64 w = 0;
      qint64 h = 0;
      if ((property.type == kBoxIspe) && parseIspe(property, w, h) && (w * h > width * height)) {
        width = w;
        height = h;
        haveExtents = true;
      }
    }
  }
  if (!haveExtents) {
    return GENERIC_ERROR;
  }

  // The clean aperture crops the coded extents; decoders hand out the
  // cropped size, so report that.
  if (haveClap && (clapWidth <= width) && (clapHeight <= height)) {
    width = clapWidth;
    height = clapHeight;
  }

  // No plain resolution in HEIF (EXIF is not parsed); one page.
  out(ImageMetadata(QSize(int(width), int(height)), Dpi()));
  return LOADED;
}  // HeicMetadataLoader::loadMetadata
