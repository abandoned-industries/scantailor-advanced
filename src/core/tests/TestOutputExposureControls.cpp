// Copyright (C) 2026 ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <QApplication>
#include <QDoubleValidator>
#include <QLineEdit>
#include <QMetaObject>
#include <QSlider>

#include <cstdio>
#include <memory>

#include "PageSelectionAccessor.h"
#include "PageSequence.h"
#include "filters/output/OptionsWidget.h"
#include "filters/output/Settings.h"

namespace {

int fail(const char* message) {
  std::fprintf(stderr, "Output exposure control regression: %s\n", message);
  return 1;
}

class EmptySelectionProvider final : public PageSelectionProvider {
 public:
  PageSequence allPages() const override { return {}; }
  std::set<PageId> selectedPages() const override { return {}; }
  std::vector<PageRange> selectedRanges() const override { return {}; }
};

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);

  auto settings = std::make_shared<output::Settings>();
  const PageSelectionAccessor pageSelectionAccessor{
      std::make_shared<EmptySelectionProvider>()};
  output::OptionsWidget options(settings, pageSelectionAccessor);

  auto* slider = options.findChild<QSlider*>(QStringLiteral("exposureSlider"));
  auto* valueField = options.findChild<QLineEdit*>(QStringLiteral("exposureValue"));
  if (!slider || !valueField) {
    return fail("the production exposure controls are unavailable");
  }
  if (slider->minimum() != -100 || slider->maximum() != 100
      || slider->singleStep() != 1) {
    return fail("the slider is not a -1.00 to +1.00 control with 0.01-stop steps");
  }

  const auto* validator = qobject_cast<const QDoubleValidator*>(valueField->validator());
  if (!validator || validator->bottom() != -1.0 || validator->top() != 1.0
      || validator->decimals() != 2) {
    return fail("the typed exposure field does not enforce the same precise range");
  }

  if (!QMetaObject::invokeMethod(&options, "photoAdjExposureChanged",
                                 Qt::DirectConnection, Q_ARG(int, 37))
      || slider->value() != 37 || valueField->text() != QStringLiteral("0.37")) {
    return fail("a hundredth-stop slider value does not reach the displayed setting");
  }

  if (!QMetaObject::invokeMethod(&options, "photoAdjExposureChanged",
                                 Qt::DirectConnection, Q_ARG(int, 101))
      || slider->value() != 100 || valueField->text() != QStringLiteral("1.00")) {
    return fail("an out-of-range UI value is not clamped consistently");
  }

  return 0;
}
