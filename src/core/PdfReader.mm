// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfReader.h"

#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <list>
#include <limits>
#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

// Static storage for PDF import DPI settings
static QMutex s_importDpiMutex;
static std::map<QString, int> s_importDpiMap;

#include "Dpi.h"
#include "ImageLoader.h"
#include "ImageMetadata.h"

#ifdef Q_OS_MACOS
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

namespace {
struct PdfDocumentKey {
  QString canonicalPath;
  qint64 fileSize;
  qint64 modificationTime;

  bool operator==(const PdfDocumentKey& other) const {
    return canonicalPath == other.canonicalPath && fileSize == other.fileSize
        && modificationTime == other.modificationTime;
  }

  bool operator<(const PdfDocumentKey& other) const {
    return std::tie(canonicalPath, fileSize, modificationTime)
        < std::tie(other.canonicalPath, other.fileSize, other.modificationTime);
  }
};

PdfDocumentKey pdfDocumentKey(const QString& filePath) {
  const QFileInfo info(filePath);
  const QString canonicalPath = info.canonicalFilePath();
  return {canonicalPath.isEmpty() ? info.absoluteFilePath() : canonicalPath, info.size(),
          info.lastModified().toMSecsSinceEpoch()};
}

// Thread-safe, small LRU of PDF documents. Shared handles keep an evicted
// document alive until any metadata scan or rasterization using it completes.
class PdfDocumentManager {
 public:
  class Document {
   public:
    explicit Document(CGPDFDocumentRef document) : m_document(document) {}
    ~Document() {
      if (m_document) {
        CGPDFDocumentRelease(m_document);
      }
    }

    CGPDFDocumentRef get() const { return m_document; }

   private:
    CGPDFDocumentRef m_document;
  };

  using DocumentHandle = std::shared_ptr<const Document>;

  static PdfDocumentManager& instance() {
    static PdfDocumentManager s_instance;
    return s_instance;
  }

  DocumentHandle acquire(const QString& filePath) {
    // Zotero stages a PDF and immediately asks LaunchServices to open its work
    // directory.  On APFS the file can already have its final name and header
    // while CoreGraphics still rejects that first, very early open.  Retry only
    // files modified moments ago so malformed, settled PDFs still fail quickly.
    constexpr int maxAttempts = 11;
    constexpr qint64 recentFileWindowMs = 2000;
    constexpr unsigned long retryDelayMs = 100;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
      const PdfDocumentKey key = pdfDocumentKey(filePath);
      {
        QMutexLocker lock(&m_mutex);
        const auto it = m_documents.find(key);
        if (it != m_documents.end()) {
          m_lru.splice(m_lru.begin(), m_lru, it->second.lruPosition);
          return it->second.document;
        }
      }

      CGPDFDocumentRef loadedDocument = nullptr;
      @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:key.canonicalPath.toNSString()];
        loadedDocument = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);
      }
      if (!loadedDocument) {
        const QFileInfo currentInfo(filePath);
        const qint64 ageMs = currentInfo.lastModified().msecsTo(QDateTime::currentDateTime());
        if (attempt + 1 < maxAttempts && ageMs >= 0 && ageMs <= recentFileWindowMs) {
          qWarning() << "PdfReader: CoreGraphics rejected newly staged PDF; retrying:"
                     << key.canonicalPath << "attempt" << attempt + 1 << "age ms" << ageMs;
          QThread::msleep(retryDelayMs);
          continue;
        }
        qWarning() << "PdfReader: Failed to load PDF:" << key.canonicalPath;
        return {};
      }

      DocumentHandle newDocument = std::make_shared<Document>(loadedDocument);
      if (!(pdfDocumentKey(filePath) == key)) {
        continue;
      }

      QMutexLocker lock(&m_mutex);
      const auto existing = m_documents.find(key);
      if (existing != m_documents.end()) {
        m_lru.splice(m_lru.begin(), m_lru, existing->second.lruPosition);
        return existing->second.document;
      }

      m_lru.push_front(key);
      m_documents.emplace(key, Entry{newDocument, m_lru.begin()});
      while (m_documents.size() > MAX_CACHED_DOCUMENTS) {
        const PdfDocumentKey evictedKey = m_lru.back();
        m_documents.erase(evictedKey);
        m_lru.pop_back();
      }
      return newDocument;
    }
    return {};
  }

 private:
  struct Entry {
    DocumentHandle document;
    std::list<PdfDocumentKey>::iterator lruPosition;
  };

  static constexpr size_t MAX_CACHED_DOCUMENTS = 4;

  PdfDocumentManager() = default;

  PdfDocumentManager(const PdfDocumentManager&) = delete;
  PdfDocumentManager& operator=(const PdfDocumentManager&) = delete;

  QMutex m_mutex;
  std::list<PdfDocumentKey> m_lru;
  std::map<PdfDocumentKey, Entry> m_documents;
};

int rasterConcurrency() {
  bool overrideOk = false;
  const int overrideValue = qEnvironmentVariableIntValue("SCANTAILOR_PDF_RASTER_CONCURRENCY", &overrideOk);
  if (overrideOk && overrideValue > 0) {
    return std::clamp(overrideValue, 1, 64);
  }

  constexpr quint64 estimatedRasterBytes = 24ULL * 1024 * 1024 * 4;
  constexpr quint64 maxRasterBudget = 2ULL * 1024 * 1024 * 1024;
  const quint64 physicalMemory = [[NSProcessInfo processInfo] physicalMemory];
  const quint64 rasterBudget = std::min(maxRasterBudget, physicalMemory / 4);
  const int memorySlots = std::max(1, static_cast<int>(rasterBudget / estimatedRasterBytes));
  const int activeCores = std::max(1, QThread::idealThreadCount());
  return std::min({activeCores, memorySlots, 8});
}

class RasterPermit {
 public:
  RasterPermit() {
    static dispatch_semaphore_t semaphore = dispatch_semaphore_create(rasterConcurrency());
    m_semaphore = semaphore;
    dispatch_semaphore_wait(m_semaphore, DISPATCH_TIME_FOREVER);
  }
  ~RasterPermit() { dispatch_semaphore_signal(m_semaphore); }

 private:
  dispatch_semaphore_t m_semaphore;
};

// Get the effective box for a PDF page (cropBox if available, otherwise mediaBox)
static CGRect getEffectiveBox(CGPDFPageRef page) {
  CGRect cropBox = CGPDFPageGetBoxRect(page, kCGPDFCropBox);
  if (CGRectIsEmpty(cropBox)) {
    return CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
  }
  return cropBox;
}

// Get the display size of a page accounting for rotation
static CGSize getDisplaySize(CGPDFPageRef page) {
  CGRect box = getEffectiveBox(page);
  int rotation = CGPDFPageGetRotationAngle(page);

  // 90 or 270 degree rotation swaps width and height
  if (rotation == 90 || rotation == 270) {
    return CGSizeMake(box.size.height, box.size.width);
  }
  return box.size;
}

static CGFloat getUserUnit(CGPDFPageRef page) {
  const CGPDFDictionaryRef dict = CGPDFPageGetDictionary(page);
  if (!dict) {
    return 1.0;
  }
  CGPDFReal userUnit = 1.0;
  if (CGPDFDictionaryGetNumber(dict, "UserUnit", &userUnit)) {
    if (userUnit > 0.0) {
      return static_cast<CGFloat>(userUnit);
    }
  }
  return 1.0;
}

// Callback context for scanning XObjects in a page's content stream
struct XObjectScanContext {
  CGPDFDictionaryRef pageResources;
  CGFloat pageWidth;   // Display width in points
  CGFloat pageHeight;  // Display height in points
  CGFloat userUnit;
  int maxDpi;
};

// Scan a Resources dictionary for image XObjects and calculate their effective DPI
static void scanResourcesForImages(CGPDFDictionaryRef resources, CGFloat pageWidth, CGFloat pageHeight,
                                    CGFloat userUnit, int& maxDpi) {
  if (!resources) return;

  CGPDFDictionaryRef xobjects = nullptr;
  if (!CGPDFDictionaryGetDictionary(resources, "XObject", &xobjects) || !xobjects) {
    return;
  }

  // Context to pass to the applier callback
  struct ApplyContext {
    CGFloat pageWidth;
    CGFloat pageHeight;
    CGFloat userUnit;
    int* maxDpi;
  };
  ApplyContext ctx = {pageWidth, pageHeight, userUnit, &maxDpi};

  // Iterate over all XObjects using block with 3 parameters
  CGPDFDictionaryApplyBlock(xobjects, ^bool(const char* key, CGPDFObjectRef value, void* info) {
    (void)key;  // Unused
    ApplyContext* ctxPtr = static_cast<ApplyContext*>(info);

    CGPDFStreamRef stream = nullptr;
    if (CGPDFObjectGetValue(value, kCGPDFObjectTypeStream, &stream) && stream) {
      CGPDFDictionaryRef streamDict = CGPDFStreamGetDictionary(stream);
      if (streamDict) {
        const char* subtype = nullptr;
        if (CGPDFDictionaryGetName(streamDict, "Subtype", &subtype) && subtype) {
          if (strcmp(subtype, "Image") == 0) {
            // Found an image! Get its dimensions
            CGPDFInteger imgWidth = 0, imgHeight = 0;
            if (CGPDFDictionaryGetInteger(streamDict, "Width", &imgWidth) &&
                CGPDFDictionaryGetInteger(streamDict, "Height", &imgHeight) &&
                imgWidth > 0 && imgHeight > 0) {
              // Calculate effective DPI
              // Assume image fills the page (conservative estimate)
              // DPI = image_pixels / (page_points / 72)
              const CGFloat pgWidthInches = (ctxPtr->pageWidth * ctxPtr->userUnit) / 72.0;
              const CGFloat pgHeightInches = (ctxPtr->pageHeight * ctxPtr->userUnit) / 72.0;

              int effectiveDpiX = (pgWidthInches > 0) ? static_cast<int>(imgWidth / pgWidthInches) : 0;
              int effectiveDpiY = (pgHeightInches > 0) ? static_cast<int>(imgHeight / pgHeightInches) : 0;

              // Use the larger dimension's DPI (more conservative)
              int effectiveDpi = std::max(effectiveDpiX, effectiveDpiY);
              if (effectiveDpi > *ctxPtr->maxDpi) {
                *ctxPtr->maxDpi = effectiveDpi;
              }
            }
          }
        }
      }
    }
    return true;  // Continue iterating
  }, &ctx);
}

// Detect effective DPI from embedded images in PDF
// Samples first few pages and returns maximum DPI found, rounded to standard values
static int detectEffectiveDpi(CGPDFDocumentRef document, int samplePages = 5) {
  if (!document) return PdfReader::DEFAULT_RENDER_DPI;

  const size_t pageCount = CGPDFDocumentGetNumberOfPages(document);
  if (pageCount == 0) return PdfReader::DEFAULT_RENDER_DPI;

  int maxDpi = 0;
  const int pagesToSample = std::min(samplePages, static_cast<int>(pageCount));

  for (int i = 1; i <= pagesToSample; ++i) {
    CGPDFPageRef page = CGPDFDocumentGetPage(document, i);
    if (!page) continue;

    CGRect mediaBox = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
    CGFloat userUnit = getUserUnit(page);

    // Get page resources dictionary
    CGPDFDictionaryRef pageDict = CGPDFPageGetDictionary(page);
    if (pageDict) {
      CGPDFDictionaryRef resources = nullptr;
      if (CGPDFDictionaryGetDictionary(pageDict, "Resources", &resources) && resources) {
        scanResourcesForImages(resources, mediaBox.size.width, mediaBox.size.height, userUnit, maxDpi);
      }
    }
  }

  // Round to nearest standard DPI value
  if (maxDpi > 500) return 600;
  if (maxDpi > 350) return 400;
  if (maxDpi > 0) return 300;

  return PdfReader::DEFAULT_RENDER_DPI;  // Default fallback
}

}  // namespace
#endif  // Q_OS_MACOS

bool PdfReader::checkMagic(const QByteArray& data) {
  return data.left(1024).indexOf("%PDF-") >= 0;
}

bool PdfReader::canRead(QIODevice& device) {
  if (!device.isOpen()) {
    if (!device.open(QIODevice::ReadOnly)) {
      return false;
    }
  }

  const qint64 origPos = device.pos();
  device.seek(0);
  const QByteArray header = device.read(1024);
  device.seek(origPos);

  return checkMagic(header);
}

bool PdfReader::canRead(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray header = file.read(1024);
  return checkMagic(header);
}

#ifdef Q_OS_MACOS

ImageMetadataLoader::Status PdfReader::readMetadata(QIODevice& device,
                                                    const VirtualFunction<void, const ImageMetadata&>& out) {
  auto* file = qobject_cast<QFile*>(&device);
  if (!file) {
    return ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;
  }
  return readMetadata(file->fileName(), out);
}

ImageMetadataLoader::Status PdfReader::readMetadata(const QString& filePath,
                                                    const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!canRead(filePath)) {
    return ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;
  }

  // Use stored import DPI if available, otherwise default
  const int renderDpi = getImportDpi(filePath);

  // Collect page metadata before callbacks.
  struct PageInfo {
    int widthPx;
    int heightPx;
  };
  std::vector<PageInfo> pages;

  const PdfDocumentManager::DocumentHandle document = PdfDocumentManager::instance().acquire(filePath);
  if (!document) {
    return ImageMetadataLoader::GENERIC_ERROR;
  }

  CGPDFDocumentRef doc = document->get();
  const size_t pageCount = CGPDFDocumentGetNumberOfPages(doc);

  if (pageCount == 0) {
    qWarning() << "PdfReader: PDF has no pages:" << filePath;
    return ImageMetadataLoader::NO_IMAGES;
  }

  constexpr double maxDimension = static_cast<double>(std::numeric_limits<int>::max());
  pages.reserve(pageCount);

  for (size_t i = 1; i <= pageCount; ++i) {
    CGPDFPageRef page = CGPDFDocumentGetPage(doc, i);
    if (!page) {
      continue;
    }

    // Get display size (accounts for rotation)
    CGSize displaySize = getDisplaySize(page);
    const CGFloat userUnit = getUserUnit(page);

    const double calcWidth = displaySize.width * renderDpi / 72.0 * userUnit + 0.5;
    const double calcHeight = displaySize.height * renderDpi / 72.0 * userUnit + 0.5;

    if (calcWidth <= 0 || calcHeight <= 0 || calcWidth > maxDimension || calcHeight > maxDimension) {
      qWarning() << "PdfReader: Invalid page dimensions for page" << i << "- skipping";
      continue;
    }

    pages.push_back({static_cast<int>(calcWidth), static_cast<int>(calcHeight)});
  }

  // Now invoke callbacks without holding the lock (prevents potential deadlocks)
  for (const PageInfo& info : pages) {
    out(ImageMetadata(QSize(info.widthPx, info.heightPx), Dpi(renderDpi, renderDpi)));
  }

  return ImageMetadataLoader::LOADED;
}

QImage PdfReader::readImage(const QString& filePath, int pageNum, int dpi) {
  const PdfDocumentManager::DocumentHandle document = PdfDocumentManager::instance().acquire(filePath);
  if (!document) {
    qWarning() << "PdfReader: Failed to load PDF for rendering:" << filePath;
    return QImage();
  }

  CGPDFDocumentRef doc = document->get();

  const size_t pageIndex = static_cast<size_t>(pageNum + 1);
  const size_t totalPages = CGPDFDocumentGetNumberOfPages(doc);

  if (pageIndex < 1 || pageIndex > totalPages) {
    qWarning() << "PdfReader: Invalid page number:" << pageNum << "for PDF with" << totalPages << "pages";
    return QImage();
  }

  CGPDFPageRef page = CGPDFDocumentGetPage(doc, pageIndex);
  if (!page) {
    qWarning() << "PdfReader: Failed to get page" << pageNum;
    return QImage();
  }

  const CGRect box = getEffectiveBox(page);
  const int rotation = CGPDFPageGetRotationAngle(page);

  // Get display size (accounts for rotation)
  CGSize displaySize = getDisplaySize(page);
  const CGFloat userUnit = getUserUnit(page);

  constexpr double maxDim = static_cast<double>(std::numeric_limits<int>::max());
  const CGFloat scale = static_cast<CGFloat>(dpi) / 72.0 * userUnit;

  const double calcWidth = displaySize.width * scale + 0.5;
  const double calcHeight = displaySize.height * scale + 0.5;

  if (calcWidth <= 0 || calcHeight <= 0 || calcWidth > maxDim || calcHeight > maxDim) {
    qWarning() << "PdfReader: Invalid render dimensions for page" << pageNum;
    return QImage();
  }

  const int widthPx = static_cast<int>(calcWidth);
  const int heightPx = static_cast<int>(calcHeight);

  RasterPermit rasterPermit;
  QImage image(widthPx, heightPx, QImage::Format_RGB32);
  image.fill(Qt::white);

  // Use autoreleasepool to prevent memory buildup during batch operations
  @autoreleasepool {
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(
        image.bits(),
        widthPx,
        heightPx,
        8,
        image.bytesPerLine(),
        colorSpace,
        kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(colorSpace);

    if (!context) {
      qWarning() << "PdfReader: Failed to create bitmap context for page" << pageNum;
      return QImage();
    }

    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);

    // Scale to requested DPI in user space (72dpi points).
    CGContextScaleCTM(context, scale, scale);

    // Manual transform: translate to box origin and apply page rotation.
    CGContextTranslateCTM(context, -box.origin.x, -box.origin.y);
    if (rotation == 90) {
      CGContextTranslateCTM(context, 0, box.size.width);
      CGContextRotateCTM(context, -M_PI_2);
    } else if (rotation == 180) {
      CGContextTranslateCTM(context, box.size.width, box.size.height);
      CGContextRotateCTM(context, M_PI);
    } else if (rotation == 270) {
      CGContextTranslateCTM(context, box.size.height, 0);
      CGContextRotateCTM(context, M_PI_2);
    }

    CGContextDrawPDFPage(context, page);
    CGContextRelease(context);
  }

  // Set DPI metadata
  const int dotsPerMeter = static_cast<int>(dpi / 0.0254 + 0.5);
  image.setDotsPerMeterX(dotsPerMeter);
  image.setDotsPerMeterY(dotsPerMeter);

  return image;
}

PdfReader::PdfInfo PdfReader::readPdfInfo(const QString& filePath) {
  PdfInfo info;

  if (!canRead(filePath)) {
    return info;
  }

  const PdfDocumentManager::DocumentHandle document = PdfDocumentManager::instance().acquire(filePath);
  if (!document) {
    return info;
  }

  CGPDFDocumentRef doc = document->get();

  info.pageCount = static_cast<int>(CGPDFDocumentGetNumberOfPages(doc));
  info.detectedDpi = detectEffectiveDpi(doc);

  qDebug() << "PdfReader: Detected DPI for" << filePath << ":" << info.detectedDpi
           << "pages:" << info.pageCount;

  return info;
}

void PdfReader::setImportDpi(const QString& filePath, int dpi) {
  {
    QMutexLocker lock(&s_importDpiMutex);
    s_importDpiMap[filePath] = dpi;
  }
  ImageLoader::invalidate(filePath);
  qDebug() << "PdfReader: Set import DPI for" << filePath << "to" << dpi;
}

int PdfReader::getImportDpi(const QString& filePath) {
  QMutexLocker lock(&s_importDpiMutex);
  auto it = s_importDpiMap.find(filePath);
  if (it != s_importDpiMap.end()) {
    return it->second;
  }
  return DEFAULT_RENDER_DPI;
}

bool PdfReader::hasImportDpi(const QString& filePath) {
  QMutexLocker lock(&s_importDpiMutex);
  return s_importDpiMap.find(filePath) != s_importDpiMap.end();
}

#endif  // Q_OS_MACOS
