#include "PdfReadError.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_MACOS
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#endif

namespace {
QString fileFailureReason(const QFile& file, int systemError) {
  QString reason = QCoreApplication::translate("PdfReadError", "%1 (Qt file error %2)")
                       .arg(file.errorString())
                       .arg(static_cast<int>(file.error()));
  if (systemError != 0) {
    reason += QCoreApplication::translate("PdfReadError", "; errno %1: %2")
                  .arg(systemError)
                  .arg(QString::fromLocal8Bit(std::strerror(systemError)));
  }
  return reason;
}
}  // namespace

PdfReadError::Diagnostic PdfReadError::diagnose(const QString& filePath) {
  QFile file(filePath);
  errno = 0;
  if (!file.open(QIODevice::ReadOnly)) {
    const int systemError = errno;
    return {Cause::FileOpenFailed, fileFailureReason(file, systemError), file.error(), systemError};
  }

  char headerBuffer[1024];
  errno = 0;
  const qint64 bytesRead = file.read(headerBuffer, sizeof(headerBuffer));
  if (bytesRead < 0) {
    const int systemError = errno;
    return {Cause::FileReadFailed, fileFailureReason(file, systemError), file.error(), systemError};
  }

  const QByteArray header(headerBuffer, static_cast<qsizetype>(bytesRead));
  if (header.indexOf("%PDF-") < 0) {
    return {Cause::HeaderMissing,
            QCoreApplication::translate(
                "PdfReadError", "No %PDF- header was found in the first 1,024 bytes."),
            QFileDevice::NoError, 0};
  }

#ifdef Q_OS_MACOS
  const QFileInfo info(filePath);
  const QString canonicalPath = info.canonicalFilePath();
  const QString loadPath = canonicalPath.isEmpty() ? info.absoluteFilePath() : canonicalPath;
  CGPDFDocumentRef document = nullptr;
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:loadPath.toNSString()];
    document = CGPDFDocumentCreateWithURL((__bridge CFURLRef)url);
  }
  if (!document) {
    return {Cause::CoreGraphicsOpenFailed,
            QCoreApplication::translate(
                "PdfReadError",
                "The file is readable and has a PDF header, but CoreGraphics could not open it. "
                "The file may still be copying or may be malformed."),
            QFileDevice::NoError, 0};
  }

  const size_t pageCount = CGPDFDocumentGetNumberOfPages(document);
  CGPDFDocumentRelease(document);
  if (pageCount == 0) {
    return {Cause::NoPages,
            QCoreApplication::translate("PdfReadError", "The PDF document contains no pages."),
            QFileDevice::NoError, 0};
  }
#endif

  return {};
}

QString PdfReadError::message(const QString& filePath) {
  const Diagnostic diagnostic = diagnose(filePath);
  const QString reason = diagnostic.cause == Cause::None
      ? QCoreApplication::translate(
            "PdfReadError", "The file passed independent validation, but the PDF reader returned no pages.")
      : diagnostic.reason;
  return QCoreApplication::translate("PdfReadError", "Failed to read PDF file.\n\nPath: %1\nReason: %2")
      .arg(QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath()), reason);
}
