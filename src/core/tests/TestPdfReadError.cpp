#include <boost/test/unit_test.hpp>

#include <QFile>
#include <QTemporaryDir>

#include "PdfReadError.h"

BOOST_AUTO_TEST_SUITE(PdfReadErrorTestSuite)

BOOST_AUTO_TEST_CASE(reports_missing_file_and_missing_header) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());

  const auto missing = PdfReadError::diagnose(tempDir.filePath("missing.pdf"));
  BOOST_CHECK(missing.cause == PdfReadError::Cause::FileOpenFailed);
  BOOST_CHECK(missing.qtError != QFileDevice::NoError);

  QFile garbage(tempDir.filePath("garbage.pdf"));
  BOOST_REQUIRE(garbage.open(QIODevice::WriteOnly));
  BOOST_REQUIRE_EQUAL(garbage.write("not a PDF"), 9);
  garbage.close();

  const auto invalid = PdfReadError::diagnose(garbage.fileName());
  BOOST_CHECK(invalid.cause == PdfReadError::Cause::HeaderMissing);
  BOOST_CHECK(invalid.reason.contains("1,024"));
}

BOOST_AUTO_TEST_CASE(explains_core_graphics_rejection) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());

  QFile truncated(tempDir.filePath("truncated.pdf"));
  BOOST_REQUIRE(truncated.open(QIODevice::WriteOnly));
  BOOST_REQUIRE_EQUAL(truncated.write("%PDF-1.7\n"), 9);
  truncated.close();

  const auto diagnostic = PdfReadError::diagnose(truncated.fileName());
  BOOST_CHECK(diagnostic.cause == PdfReadError::Cause::CoreGraphicsOpenFailed);
  BOOST_CHECK(diagnostic.reason.contains("still be copying"));
  BOOST_CHECK(diagnostic.reason.contains("malformed"));
}

BOOST_AUTO_TEST_CASE(accepts_bom_prefixed_pdf_in_private_tmp) {
  QFile source(QStringLiteral(SCANTAILOR_TEST_SOURCE_DIR "/ScanTailor Spectre Readme.pdf"));
  BOOST_REQUIRE(source.open(QIODevice::ReadOnly));
  const QByteArray sourceData = source.readAll();
  BOOST_REQUIRE(sourceData.startsWith("%PDF-"));

  QTemporaryDir tempDir(QStringLiteral("/private/tmp/scantailor-pdf-diagnostic-XXXXXX"));
  BOOST_REQUIRE(tempDir.isValid());
  QFile copy(tempDir.filePath("bom.pdf"));
  BOOST_REQUIRE(copy.open(QIODevice::WriteOnly));
  BOOST_REQUIRE_EQUAL(copy.write("\xEF\xBB\xBF", 3), 3);
  BOOST_REQUIRE_EQUAL(copy.write(sourceData), sourceData.size());
  copy.close();

  const auto diagnostic = PdfReadError::diagnose(copy.fileName());
  BOOST_CHECK(diagnostic.cause == PdfReadError::Cause::None);
}

BOOST_AUTO_TEST_SUITE_END()
