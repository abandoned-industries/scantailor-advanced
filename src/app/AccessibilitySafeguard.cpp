// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AccessibilitySafeguard.h"

#include <QtGui/qtguiglobal.h>

#if defined(Q_OS_MAC) && QT_CONFIG(accessibility)

#include <QAbstractItemView>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QByteArray>
#include <QString>
#include <QTabBar>

namespace {

// ---------------------------------------------------------------------------
// Why this exists
// ---------------------------------------------------------------------------
// Qt 6.11.1's macOS accessibility bridge crashes (SIGSEGV) as soon as any
// external accessibility client reads AXSelectedChildren from a widget backed
// by QAccessibleTable (every QAbstractItemView: QTableView, QTreeView,
// QListView and their QTableWidget/QTreeWidget/QListWidget wrappers).
//
// Mechanism, verified by disassembly and by an in-process reproducer:
//
//   -[QMacAccessibilityElement accessibilitySelectedChildren]
//       iface = QAccessible::accessibleInterface(axid)
//       sel   = iface->interface_cast(QAccessible::SelectionInterface)
//       for (QAccessibleInterface *p : sel->selectedItems())
//           if (p && p->isValid())   <-- SIGSEGV here
//
// QAccessibleTable::selectedCells() builds that list from its childToId cache
// via QAccessible::accessibleInterface(id). In Qt 6.11.1 that cache hands back
// QAccessibleTableCell objects that have already been freed, so the p->isValid()
// virtual call dereferences a malloc freelist word. The faulting instruction is
// `ldr x8, [x8, #0x10]` at accessibilitySelectedChildren+196.
//
// This is not caused by anything ScanTailor does: a thirty-line stock Qt 6.11.1
// application containing one never-modified QTableView with a selected row dies
// on the very first AXSelectedChildren probe, 100% of the time. ScanTailor is
// hit because it has item views (the stage list, the relinking list, the page
// summary dialogs) and because macOS automation/assistive clients routinely walk
// the whole hierarchy of a freshly launched app.
//
// Neither QAccessibleTable nor qAccessibleFactory() is exported from QtWidgets,
// so the broken interface cannot be subclassed or wrapped. The one supported
// lever is QAccessible::installFactory(): a factory installed after QApplication
// is constructed is consulted before Qt's own widget factory, so we can supply a
// different QAccessibleInterface for exactly the widget classes that reach the
// broken code, and let every other widget keep Qt's normal accessible.
//
// What we give up: item views (and tab bars, whose QAccessibleTabBar shares the
// same selectedItems() plumbing) are reported to VoiceOver as a plain widget
// with a role instead of an addressable grid of cells. Everything else - labels,
// buttons, text fields, menus, dialogs, window structure, focus - is untouched.
// That is a deliberate trade: without it, any accessibility client, VoiceOver
// included, terminates the application within seconds of launch.
//
// Set SCANTAILOR_ENABLE_QT_ITEMVIEW_A11Y=1 to skip the safeguard and get Qt's
// stock behaviour back; drop this file entirely once the upstream Qt fix ships.
// ---------------------------------------------------------------------------

QAccessibleInterface* safeInterfaceFactory(const QString& /*classname*/, QObject* object) {
  // Item views: replaces QAccessibleTable / QAccessibleList / QAccessibleTree,
  // all of which expose the crashing QAccessibleSelectionInterface.
  if (auto* view = qobject_cast<QAbstractItemView*>(object)) {
    return new QAccessibleWidget(view, QAccessible::List);
  }
  // Tab bars: QAccessibleTabBar caches child interfaces the same way and feeds
  // the same accessibilitySelectedChildren path.
  if (auto* tabBar = qobject_cast<QTabBar*>(object)) {
    return new QAccessibleWidget(tabBar, QAccessible::PageTabList);
  }
  // Everything else keeps Qt's own accessible.
  return nullptr;
}

}  // namespace

void installAccessibilitySafeguard() {
  if (qgetenv("SCANTAILOR_ENABLE_QT_ITEMVIEW_A11Y").toInt() != 0) {
    return;
  }
  QAccessible::installFactory(&safeInterfaceFactory);
}

#else  // !Q_OS_MAC || no accessibility support

void installAccessibilitySafeguard() {}

#endif
