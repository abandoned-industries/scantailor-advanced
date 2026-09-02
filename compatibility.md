# macOS 11 Support: CGPDFDocument Implementation

 

**Goal**: Enable macOS 11 (Big Sur) support by replacing Qt::Pdf with native CoreGraphics APIs.

 

---

 

## Problem

 

The app currently requires macOS 12+ because `PdfReader.cpp` uses `QPdfDocument` from the Qt::Pdf module, which is built on Chromium/PDFium and requires macOS 12.

 

| Component | Current API | macOS Requirement |

|-----------|-------------|-------------------|

| PDF Reading | `QPdfDocument` (Qt::Pdf) | **macOS 12+** |

| PDF Export | `QPdfWriter` (Qt::Gui) | macOS 10.14+ ✓ |

 

## Solution

 

Replace `QPdfDocument` with `CGPDFDocument` (CoreGraphics), which is available since **macOS 10.4**.

 

---

 

## Implementation

 

### 1. Rename File

 

```bash

mv src/core/PdfReader.cpp src/core/PdfReader.mm

```

 

### 2. Update CMakeLists.txt

 

**File**: `CMakeLists.txt` (root)

 

Remove `Pdf` from Qt6 components (line ~192):

 

```cmake

# Before

find_package(

    Qt6 6.4 REQUIRED

    COMPONENTS Core Gui Widgets Xml Network LinguistTools OpenGL Svg OpenGLWidgets Pdf

    CONFIG)

 

# After

find_package(

    Qt6 6.4 REQUIRED

    COMPONENTS Core Gui Widgets Xml Network LinguistTools OpenGL Svg OpenGLWidgets

    CONFIG)

```

 

Remove `Qt_Pdf_lib` variable (line ~205):

 

```cmake

# Delete this line

set(Qt_Pdf_lib Qt::Pdf)

```

 

**File**: `src/core/CMakeLists.txt`

 

Update source file extension:

 

```cmake

# Before

PdfReader.cpp PdfReader.h

 

# After

PdfReader.mm PdfReader.h

```

 

Remove Qt::Pdf from target link libraries (line ~159):

 

```cmake

# Before

PUBLIC Qt::Core Qt::Gui Qt::Widgets Qt::Xml Qt::Network Qt::OpenGL Qt::Svg Qt::OpenGLWidgets Qt::Pdf

 

# After

PUBLIC Qt::Core Qt::Gui Qt::Widgets Qt::Xml Qt::Network Qt::OpenGL Qt::Svg Qt::OpenGLWidgets

```

 

Add CoreGraphics framework (macOS only):

 

```cmake

if(APPLE)

  target_link_libraries(core PUBLIC "-framework CoreGraphics" "-framework Foundation")

endif()

```

 

### 3. Replace PdfReader Implementation

 

**File**: `src/core/PdfReader.mm`

 

```objc

// Copyright (C) 2024  ScanTailor Advanced contributors

// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

 

#include "PdfReader.h"

 

#include <QDebug>

#include <QFile>

#include <QIODevice>

#include <QImage>

#include <QMutex>

#include <QMutexLocker>

 

#include <limits>

 

#include "Dpi.h"

#include "ImageMetadata.h"

 

#ifdef Q_OS_MACOS

#import <CoreGraphics/CoreGraphics.h>

#import <Foundation/Foundation.h>

 

// Thread-safe PDF document manager using CGPDFDocument.

// CoreGraphics PDF APIs are not thread-safe, so we serialize access.

class PdfDocumentManager {

 public:

  static PdfDocumentManager& instance() {

    static PdfDocumentManager s_instance;

    return s_instance;

  }

 

  QMutex& mutex() { return m_mutex; }

 

  bool ensureLoaded(const QString& filePath) {

    // If already loaded, return success

    if (m_loadedFilePath == filePath && m_doc) {

      return true;

    }

 

    // Close any previously loaded document

    if (m_doc) {

      CGPDFDocumentRelease(m_doc);

      m_doc = nullptr;

    }

    m_loadedFilePath.clear();

 

    // Load the new document

    @autoreleasepool {

      NSString* nsPath = filePath.toNSString();

      NSURL* url = [NSURL fileURLWithPath:nsPath];

      m_doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);

    }

 

    if (!m_doc) {

      qDebug() << "PdfReader: Failed to load PDF:" << filePath;

      return false;

    }

 

    m_loadedFilePath = filePath;

    return true;

  }

 

  CGPDFDocumentRef document() { return m_doc; }

 

  size_t pageCount() {

    return m_doc ? CGPDFDocumentGetNumberOfPages(m_doc) : 0;

  }

 

 private:

  PdfDocumentManager() = default;

  ~PdfDocumentManager() {

    if (m_doc) {

      CGPDFDocumentRelease(m_doc);

    }

  }

 

  // Non-copyable, non-movable

  PdfDocumentManager(const PdfDocumentManager&) = delete;

  PdfDocumentManager& operator=(const PdfDocumentManager&) = delete;

 

  QMutex m_mutex;

  CGPDFDocumentRef m_doc = nullptr;

  QString m_loadedFilePath;

};

 

#endif  // Q_OS_MACOS

 

bool PdfReader::checkMagic(const QByteArray& data) {

  // PDF files start with "%PDF-"

  return data.size() >= 5 && data.startsWith("%PDF-");

}

 

bool PdfReader::canRead(QIODevice& device) {

  if (!device.isOpen()) {

    if (!device.open(QIODevice::ReadOnly)) {

      return false;

    }

  }

 

  const qint64 origPos = device.pos();

  device.seek(0);

  const QByteArray header = device.read(5);

  device.seek(origPos);

 

  return checkMagic(header);

}

 

bool PdfReader::canRead(const QString& filePath) {

  QFile file(filePath);

  if (!file.open(QIODevice::ReadOnly)) {

    return false;

  }

  const QByteArray header = file.read(5);

  return checkMagic(header);

}

 

#ifdef Q_OS_MACOS

 

ImageMetadataLoader::Status PdfReader::readMetadata(QIODevice& device,

                                                    const VirtualFunction<void, const ImageMetadata&>& out) {

  // CGPDFDocument requires a file path, not a QIODevice

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

 

  PdfDocumentManager& mgr = PdfDocumentManager::instance();

  QMutexLocker lock(&mgr.mutex());

 

  if (!mgr.ensureLoaded(filePath)) {

    return ImageMetadataLoader::GENERIC_ERROR;

  }

 

  CGPDFDocumentRef doc = mgr.document();

  const size_t pageCount = CGPDFDocumentGetNumberOfPages(doc);

 

  if (pageCount == 0) {

    qDebug() << "PdfReader: PDF has no pages:" << filePath;

    return ImageMetadataLoader::NO_IMAGES;

  }

 

  // Report metadata for each page

  constexpr double maxDimension = static_cast<double>(std::numeric_limits<int>::max());

 

  for (size_t i = 1; i <= pageCount; ++i) {

    CGPDFPageRef page = CGPDFDocumentGetPage(doc, i);

    if (!page) {

      continue;

    }

 

    // Get page size in points (1/72 inch)

    CGRect mediaBox = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);

 

    // Calculate pixel dimensions at default render DPI with overflow protection

    const double calcWidth = mediaBox.size.width * DEFAULT_RENDER_DPI / 72.0 + 0.5;

    const double calcHeight = mediaBox.size.height * DEFAULT_RENDER_DPI / 72.0 + 0.5;

 

    if (calcWidth <= 0 || calcHeight <= 0 || calcWidth > maxDimension || calcHeight > maxDimension) {

      qWarning() << "PdfReader: Invalid page dimensions for page" << i << "- skipping";

      continue;

    }

 

    const int widthPx = static_cast<int>(calcWidth);

    const int heightPx = static_cast<int>(calcHeight);

 

    const QSize size(widthPx, heightPx);

    const Dpi dpi(DEFAULT_RENDER_DPI, DEFAULT_RENDER_DPI);

 

    out(ImageMetadata(size, dpi));

  }

 

  return ImageMetadataLoader::LOADED;

}

 

QImage PdfReader::readImage(const QString& filePath, int pageNum, int dpi) {

  PdfDocumentManager& mgr = PdfDocumentManager::instance();

  QMutexLocker lock(&mgr.mutex());

 

  if (!mgr.ensureLoaded(filePath)) {

    qDebug() << "PdfReader: Failed to load PDF for rendering:" << filePath;

    return QImage();

  }

 

  CGPDFDocumentRef doc = mgr.document();

 

  // CGPDFDocument pages are 1-indexed

  const size_t pageIndex = static_cast<size_t>(pageNum + 1);

  const size_t totalPages = CGPDFDocumentGetNumberOfPages(doc);

 

  if (pageIndex < 1 || pageIndex > totalPages) {

    qDebug() << "PdfReader: Invalid page number:" << pageNum << "for PDF with" << totalPages << "pages";

    return QImage();

  }

 

  CGPDFPageRef page = CGPDFDocumentGetPage(doc, pageIndex);

  if (!page) {

    qDebug() << "PdfReader: Failed to get page" << pageNum;

    return QImage();

  }

 

  // Get page size in points and calculate pixel size at requested DPI

  CGRect mediaBox = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);

 

  constexpr double maxDim = static_cast<double>(std::numeric_limits<int>::max());

  const double calcWidth = mediaBox.size.width * dpi / 72.0 + 0.5;

  const double calcHeight = mediaBox.size.height * dpi / 72.0 + 0.5;

 

  if (calcWidth <= 0 || calcHeight <= 0 || calcWidth > maxDim || calcHeight > maxDim) {

    qWarning() << "PdfReader: Invalid render dimensions for page" << pageNum;

    return QImage();

  }

 

  const int widthPx = static_cast<int>(calcWidth);

  const int heightPx = static_cast<int>(calcHeight);

 

  // Create QImage with RGB32 format (compatible with CGBitmapContext)

  QImage image(widthPx, heightPx, QImage::Format_RGB32);

  image.fill(Qt::white);

 

  // Create CGContext that draws directly into QImage's buffer

  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

  CGContextRef context = CGBitmapContextCreate(

      image.bits(),

      widthPx,

      heightPx,

      8,                        // bits per component

      image.bytesPerLine(),

      colorSpace,

      kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host);

  CGColorSpaceRelease(colorSpace);

 

  if (!context) {

    qDebug() << "PdfReader: Failed to create bitmap context for page" << pageNum;

    return QImage();

  }

 

  // Set up transform: scale to DPI and flip Y-axis (PDF origin is bottom-left)

  const CGFloat scale = static_cast<CGFloat>(dpi) / 72.0;

  CGContextScaleCTM(context, scale, -scale);

  CGContextTranslateCTM(context, 0, -mediaBox.size.height);

 

  // Handle page rotation if present

  const int rotation = CGPDFPageGetRotationAngle(page);

  if (rotation != 0) {

    // Apply rotation transform around center

    CGContextTranslateCTM(context, mediaBox.size.width / 2, mediaBox.size.height / 2);

    CGContextRotateCTM(context, -rotation * M_PI / 180.0);

    CGContextTranslateCTM(context, -mediaBox.size.width / 2, -mediaBox.size.height / 2);

  }

 

  // Draw the PDF page

  CGContextDrawPDFPage(context, page);

  CGContextRelease(context);

 

  // Set the DPI in the image metadata

  const int dotsPerMeter = static_cast<int>(dpi / 0.0254 + 0.5);

  image.setDotsPerMeterX(dotsPerMeter);

  image.setDotsPerMeterY(dotsPerMeter);

 

  return image;

}

 

#else  // Non-macOS platforms

 

ImageMetadataLoader::Status PdfReader::readMetadata(QIODevice&,

                                                    const VirtualFunction<void, const ImageMetadata&>&) {

  return ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;

}

 

ImageMetadataLoader::Status PdfReader::readMetadata(const QString&,

                                                    const VirtualFunction<void, const ImageMetadata&>&) {

  return ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;

}

 

QImage PdfReader::readImage(const QString&, int, int) {

  return QImage();

}

 

#endif  // Q_OS_MACOS

```

 

---

 

## Additional Updates

 

### 4. Update Info.plist.in

 

**File**: `packaging/macos/Info.plist.in` (line 24)

 

```xml

<key>LSMinimumSystemVersion</key>

<string>11.0</string>

```

 

### 5. Update GitHub Actions Workflow

 

**File**: `.github/workflows/macos-build.yml`

 

ARM64 build (line 49):

```yaml

-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \

```

 

Intel build (line 122):

```yaml

-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \

```

 

### 6. Update README.md

 

```markdown

**Version 2.0a12** | macOS (Apple Silicon) | Requires macOS 11 or later

```

 

---

 

## API Comparison

 

| Aspect | Qt::Pdf (QPdfDocument) | CoreGraphics (CGPDFDocument) |

|--------|------------------------|------------------------------|

| macOS minimum | 12.0 | **10.4** |

| Dependency | Qt::Pdf module (Chromium) | Built-in |

| Thread safety | Not thread-safe | Not thread-safe |

| Page indexing | 0-based | 1-based |

| Rendering | `doc.render(page, size)` | `CGContextDrawPDFPage()` |

| Memory management | Qt automatic | Manual (CFRelease) |

 

---

 

## Testing Checklist

 

- [ ] Build succeeds without Qt::Pdf module

- [ ] PDF import works (single page)

- [ ] PDF import works (multi-page)

- [ ] PDF with rotation renders correctly

- [ ] PDF metadata (page count, dimensions) correct

- [ ] App launches on macOS 11 (Big Sur)

- [ ] App launches on macOS 12 (Monterey)

- [ ] App launches on macOS 13 (Ventura)

- [ ] App launches on macOS 14 (Sonoma)

- [ ] App launches on macOS 15 (Sequoia)

 

---

 

## References

 

- [CGPDFDocument | Apple Developer Documentation](https://developer.apple.com/documentation/coregraphics/cgpdfdocument)

- [Correctly Drawing PDFs in Cocoa](https://ryanbritton.com/2015/09/correctly-drawing-pdfs-in-cocoa/)

 

Ready at: