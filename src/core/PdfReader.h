// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFREADER_H_
#define SCANTAILOR_CORE_PDFREADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QByteArray;
class QIODevice;
class QImage;
class ImageMetadata;
class QString;

class PdfReader {
 public:
  // Default DPI for rendering PDF pages (300 is standard scanning resolution)
  static constexpr int DEFAULT_RENDER_DPI = 300;

  // Info returned when analyzing a PDF file
  struct PdfInfo {
    int pageCount = 0;
    int detectedDpi = DEFAULT_RENDER_DPI;  // Effective DPI of embedded images
  };

  /**
   * \brief Analyzes a PDF file to get page count and detect optimal render DPI.
   *
   * Scans first few pages for embedded images and calculates their effective
   * resolution (image_pixels / display_inches). Returns the maximum found,
   * rounded to standard values (300, 400, 600).
   *
   * \param filePath Path to the PDF file.
   * \return PdfInfo with page count and detected DPI.
   */
  static PdfInfo readPdfInfo(const QString& filePath);

  static bool canRead(QIODevice& device);

  static bool canRead(const QString& filePath);

  static ImageMetadataLoader::Status readMetadata(QIODevice& device,
                                                  const VirtualFunction<void, const ImageMetadata&>& out);

  static ImageMetadataLoader::Status readMetadata(const QString& filePath,
                                                  const VirtualFunction<void, const ImageMetadata&>& out);

  /**
   * \brief Renders a PDF page to a QImage.
   *
   * \param filePath The path to the PDF file.
   * \param pageNum A zero-based page number within the PDF.
   * \param dpi The resolution to render at (default 300 DPI).
   * \return The resulting image, or a null image in case of failure.
   */
  static QImage readImage(const QString& filePath, int pageNum = 0, int dpi = DEFAULT_RENDER_DPI);

  /**
   * \brief Sets the import DPI for a specific PDF file.
   *
   * When a PDF is imported, the user-selected DPI is stored here.
   * Subsequent calls to readImage for this file will use this DPI
   * unless explicitly overridden.
   *
   * \param filePath The absolute path to the PDF file.
   * \param dpi The DPI to use for rendering.
   */
  static void setImportDpi(const QString& filePath, int dpi);

  /**
   * \brief Gets the import DPI for a specific PDF file.
   *
   * \param filePath The absolute path to the PDF file.
   * \return The stored DPI, or DEFAULT_RENDER_DPI if not set.
   */
  static int getImportDpi(const QString& filePath);

  /**
   * \brief Whether an import DPI has been explicitly set for this file.
   *
   * Used by ProjectWriter to persist the chosen render DPI into the project
   * XML (QW5); files without an entry render at DEFAULT_RENDER_DPI and are
   * saved without the attribute.
   */
  static bool hasImportDpi(const QString& filePath);

 private:
  static bool checkMagic(const QByteArray& data);
};

#endif  // SCANTAILOR_CORE_PDFREADER_H_
