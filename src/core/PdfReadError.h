#ifndef SCANTAILOR_CORE_PDFREADERROR_H_
#define SCANTAILOR_CORE_PDFREADERROR_H_

#include <QFileDevice>
#include <QString>

class PdfReadError {
 public:
  enum class Cause {
    None,
    FileOpenFailed,
    FileReadFailed,
    HeaderMissing,
    CoreGraphicsOpenFailed,
    NoPages
  };

  struct Diagnostic {
    Cause cause = Cause::None;
    QString reason;
    QFileDevice::FileError qtError = QFileDevice::NoError;
    int systemError = 0;
  };

  static Diagnostic diagnose(const QString& filePath);
  static QString message(const QString& filePath);
};

#endif  // SCANTAILOR_CORE_PDFREADERROR_H_
