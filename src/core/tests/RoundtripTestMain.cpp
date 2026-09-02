// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Test main for project_roundtrip_tests (Stage 1.5 characterization).
//
// The 10 filter constructors build their OptionsWidgets, so a QApplication is
// required; the offscreen QPA platform keeps this runnable headless (same
// pattern as export_options_layout_tests). The QApplication lives on main's
// stack so it is destroyed before static teardown — a function-local static
// QApplication crashes at exit. QSettings is sandboxed because several widgets
// (page_layout link buttons, CollapsibleGroupBox destructors) read and WRITE
// QSettings as a side effect of construction/teardown.

#define BOOST_TEST_NO_MAIN
#include <boost/test/unit_test.hpp>

#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdlib>

namespace {

bool initUnitTest() {
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  // Do not override an explicitly chosen platform (e.g. from CTest env).
  setenv("QT_QPA_PLATFORM", "offscreen", /*overwrite=*/0);

  QTemporaryDir settingsDir;
  QApplication::setOrganizationName(QStringLiteral("ScanTailorSpectreTests"));
  QApplication::setApplicationName(QStringLiteral("project_roundtrip_tests"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

  int qtArgc = 1;
  char arg0[] = "project_roundtrip_tests";
  char* qtArgv[] = {arg0, nullptr};
  QApplication app(qtArgc, qtArgv);

  return boost::unit_test::unit_test_main(&initUnitTest, argc, argv);
}
