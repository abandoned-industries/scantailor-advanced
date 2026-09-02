// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_TEMPOUTPUTCLEANUP_H_
#define SCANTAILOR_APP_TEMPOUTPUTCLEANUP_H_

#include <QString>

class StageSequence;
class QWidget;

// Temp-output cleanup on project close, moved verbatim out of MainWindow.
// CAUTION: cleanupTempOutputFiles performs QDir::removeRecursively(); the
// isSpectreTempOutputDir prefix guard is the safety gate and must stay the
// first check on any deletion path.
namespace temp_cleanup {

bool isSpectreTempOutputDir(const QString& path);

bool showTempCleanupWarning(QWidget* parent);

void cleanupTempOutputFiles(StageSequence* stages, const QString& defaultOutDir, QWidget* dialogParent);

}  // namespace temp_cleanup

#endif  // SCANTAILOR_APP_TEMPOUTPUTCLEANUP_H_
