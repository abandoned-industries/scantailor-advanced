// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_ACCESSIBILITYSAFEGUARD_H_
#define SCANTAILOR_APP_ACCESSIBILITYSAFEGUARD_H_

/**
 * Installs a QAccessible interface factory that keeps the application alive
 * when an external macOS accessibility client walks its UI hierarchy.
 *
 * Must be called once, after QApplication has been constructed and before any
 * widget is created. See AccessibilitySafeguard.cpp for the full rationale.
 */
void installAccessibilitySafeguard();

#endif  // ifndef SCANTAILOR_APP_ACCESSIBILITYSAFEGUARD_H_
