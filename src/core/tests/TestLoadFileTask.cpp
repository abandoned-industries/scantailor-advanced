// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Gating test for QW3 (audit §5.2, §10.15(a)): LoadFileTask must not decode
// when it is already cancelled.
//
// Before QW3, LoadFileTask::operator() ran ImageLoader::load BEFORE the first
// throwIfCancelled, so a cancelled task still paid a full decode (for PDFs, a
// whole-page rasterization). The decode is observable without any production
// seam through ImageLoader's statistics counters: every real decode registers
// a cache miss + leader decode in the process-global DecodedImageCache.
//
// Stage 1.5 deliberately skipped this test ("LoadFileTask construction was
// judged heavy"); it turns out the constructor only stores its arguments, and
// the cancelled path touches none of the heavyweight collaborators, so null
// collaborators are safe here (nothing beyond the cancel check ever runs).

#include <ImageLoader.h>
#include <LoadFileTask.h>

#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#include "Dpi.h"
#include "ImageId.h"
#include "ImageMetadata.h"
#include "PageId.h"
#include "PageInfo.h"
#include "filters/fix_orientation/Task.h"

namespace Tests {
namespace {

PageInfo makePageInfo(const QString& filePath) {
  const ImageId imageId(filePath, 0);
  const PageId pageId(imageId, PageId::SINGLE_PAGE);
  const ImageMetadata metadata(QSize(8, 8), Dpi(300, 300));
  return PageInfo(pageId, metadata, 1, false, false);
}

std::shared_ptr<fix_orientation::Task> makeStubNextTask(const PageInfo& pageInfo) {
  // Never invoked on the paths under test; the ctor only stores its arguments.
  return std::make_shared<fix_orientation::Task>(pageInfo.id(), nullptr, nullptr, nullptr, nullptr, false);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(LoadFileTaskTestSuite)

// Control case proving the observable: a NON-cancelled task on an unloadable
// file does attempt a decode (statistics move) and returns an ErrorResult.
BOOST_AUTO_TEST_CASE(uncancelled_task_decodes) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QString missingFile = temp.path() + QStringLiteral("/no-such-image.png");
  const PageInfo pageInfo = makePageInfo(missingFile);

  LoadFileTask task(BackgroundTask::INTERACTIVE, pageInfo, nullptr, nullptr, makeStubNextTask(pageInfo));

  const ImageLoader::Statistics before = ImageLoader::statistics();
  const FilterResultPtr result = task();
  const ImageLoader::Statistics after = ImageLoader::statistics();

  BOOST_CHECK(result != nullptr);  // ErrorResult for the unloadable file
  BOOST_CHECK_GT(after.leaderDecodes, before.leaderDecodes);
}

// The QW3 gate: a task cancelled BEFORE it runs must return nullptr without
// decoding anything — the statistics counters must not move at all, even
// though the file is a perfectly loadable image.
BOOST_AUTO_TEST_CASE(precancelled_task_does_not_decode) {
  QTemporaryDir temp;
  BOOST_REQUIRE(temp.isValid());
  const QString imageFile = temp.path() + QStringLiteral("/real-image.png");
  {
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::white);
    BOOST_REQUIRE(image.save(imageFile, "PNG"));
  }
  const PageInfo pageInfo = makePageInfo(imageFile);

  LoadFileTask task(BackgroundTask::INTERACTIVE, pageInfo, nullptr, nullptr, makeStubNextTask(pageInfo));
  task.cancel();

  const ImageLoader::Statistics before = ImageLoader::statistics();
  const FilterResultPtr result = task();
  const ImageLoader::Statistics after = ImageLoader::statistics();

  BOOST_CHECK(result == nullptr);
  BOOST_CHECK_EQUAL(after.leaderDecodes, before.leaderDecodes);
  BOOST_CHECK_EQUAL(after.cacheMisses, before.cacheMisses);
  BOOST_CHECK_EQUAL(after.cacheHits, before.cacheHits);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
