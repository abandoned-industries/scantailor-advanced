// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Test main for output_golden_tests. (The former `--d2-probe` child-process
// mode was removed with QW1: the D2 configuration renders now and is covered
// in-process by TestOutputGoldenD2Probe.cpp plus the mixed_split_orig_bg
// golden cell.)

#define BOOST_TEST_NO_MAIN
#include <boost/test/unit_test.hpp>

#include <QCoreApplication>

namespace {

bool initUnitTest() {
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  return boost::unit_test::unit_test_main(&initUnitTest, argc, argv);
}
