// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Characterization test for DeviationProvider (Stage 1.5, refactor proposal QW2).
//
// Pins the CURRENT single-threaded semantics of DeviationProvider<K> so that the
// planned locking fix (audit §10.16 shape (a): provider-internal mutex) can be
// proven to compute identical statistics. The class itself is documented to be
// racy when read from the GUI thread while workers write (prereqs Part 2a);
// nothing here exercises concurrency — these are the value semantics only.

#include <DeviationProvider.h>

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <limits>

namespace Tests {
namespace {

constexpr double kEps = 1e-12;

DeviationProvider<int> makeProvider() {
  // computeValueByKey: identity on the key, so addOrUpdate(key) stores `key`.
  return DeviationProvider<int>([](const int& key) { return static_cast<double>(key); });
}

}  // namespace

BOOST_AUTO_TEST_SUITE(DeviationProviderTestSuite)

// A key that was never added is never deviant and has deviation value -1.
BOOST_AUTO_TEST_CASE(missing_key_semantics) {
  DeviationProvider<int> provider = makeProvider();
  BOOST_CHECK(!provider.isDeviant(7));
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(7), -1.0, kEps);

  provider.addOrUpdate(1, 5.0);
  BOOST_CHECK(!provider.isDeviant(7));
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(7), -1.0, kEps);
}

// Fewer than 3 entries: isDeviant is always false, even for a wild outlier.
// Fewer than 2 entries: getDeviationValue returns exactly 0 for a present key.
BOOST_AUTO_TEST_CASE(small_population_semantics) {
  DeviationProvider<int> provider = makeProvider();
  provider.addOrUpdate(1, 1000.0);
  BOOST_CHECK_SMALL(provider.getDeviationValue(1), kEps);  // size < 2 -> 0.0
  BOOST_CHECK(!provider.isDeviant(1));

  provider.addOrUpdate(2, 0.0);
  BOOST_CHECK(!provider.isDeviant(1));  // size < 3 -> still false
  // size >= 2: deviation value is |value - mean| = |1000 - 500| = 500.
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(1), 500.0, kEps);
}

// Mean and *sample* standard deviation (N-1 denominator) over {1,2,3,10}:
// mean = 4, stddev = sqrt(50/3). Deviation value is |value - mean|.
BOOST_AUTO_TEST_CASE(statistics_pinned_values) {
  DeviationProvider<int> provider = makeProvider();
  for (int key : {1, 2, 3, 10}) {
    provider.addOrUpdate(key);  // via computeValueByKey (identity)
  }

  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(10), 6.0, kEps);
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(1), 3.0, kEps);

  const double stddev = std::sqrt(50.0 / 3.0);
  // isDeviant: |value - mean| > max(coef * stddev, (threshold/100) * mean).
  BOOST_CHECK(provider.isDeviant(10, 1.0, 0.0));       // 6 > 4.082...
  BOOST_CHECK(!provider.isDeviant(2, 1.0, 0.0));       // 2 < 4.082...
  BOOST_CHECK(!provider.isDeviant(10, 1.5, 0.0));      // 6 < 6.124...
  BOOST_CHECK(provider.isDeviant(10, 1.0, 100.0));     // 6 > max(4.08, 4.0)
  BOOST_CHECK(!provider.isDeviant(10, 0.0, 200.0));    // 6 < (200/100)*4 = 8
  BOOST_CHECK_GT(6.0, stddev);                          // documents the margin
}

// Statistics are recomputed lazily after every addOrUpdate/remove.
BOOST_AUTO_TEST_CASE(recompute_after_update_and_remove) {
  DeviationProvider<int> provider = makeProvider();
  provider.addOrUpdate(1, 1.0);
  provider.addOrUpdate(2, 2.0);
  provider.addOrUpdate(3, 3.0);
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(3), 1.0, kEps);  // mean 2

  provider.addOrUpdate(3, 9.0);  // overwrite: {1, 2, 9}, mean 4
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(3), 5.0, kEps);
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(1), 3.0, kEps);

  provider.remove(3);  // {1, 2}, mean 1.5
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(3), -1.0, kEps);
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(1), 0.5, kEps);

  provider.remove(42);  // removing a missing key is a no-op
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(1), 0.5, kEps);

  provider.clear();
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(1), -1.0, kEps);
  BOOST_CHECK(!provider.isDeviant(1));
}

// NaN entries: isDeviant returns the caller's default, getDeviationValue -1,
// and NaN values are excluded from the mean/stddev of other keys.
BOOST_AUTO_TEST_CASE(nan_value_semantics) {
  DeviationProvider<int> provider = makeProvider();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  provider.addOrUpdate(1, 1.0);
  provider.addOrUpdate(2, 3.0);
  provider.addOrUpdate(3, nan);
  provider.addOrUpdate(4, 5.0);

  BOOST_CHECK(!provider.isDeviant(3, 1.0, 0.0, false));
  BOOST_CHECK(provider.isDeviant(3, 1.0, 0.0, true));
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(3), -1.0, kEps);

  // Mean over {1, 3, 5} = 3; the NaN never contaminates it.
  BOOST_CHECK_CLOSE_FRACTION(provider.getDeviationValue(4), 2.0, kEps);
  BOOST_CHECK_SMALL(provider.getDeviationValue(2), kEps);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
