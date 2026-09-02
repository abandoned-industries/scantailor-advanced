// Copyright (C) 2026  ScanTailor Spectre contributors.
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_AUTOACCEPT_H_
#define SCANTAILOR_APP_AUTOACCEPT_H_

#include "PageView.h"

class BatchSummaries;
class ProjectPages;
class StageSequence;

// Auto Mode's per-stage auto-accept helpers, moved verbatim out of MainWindow.
// Each is a self-contained walk over pages writing one filter's Settings;
// remediation is applied through the BatchSummaries actions they always used.
namespace auto_accept {

void autoAcceptPageSplit(StageSequence& stages, ProjectPages& pages, BatchSummaries& summaries);

void autoSetDeskewZero(StageSequence& stages, ProjectPages& pages, PageView currentView);

void autoAcceptContentOutliers(StageSequence& stages, ProjectPages& pages, PageView currentView, BatchSummaries& summaries);

void autoAcceptPageSizeOutliers(StageSequence& stages, BatchSummaries& summaries);

}  // namespace auto_accept

#endif  // SCANTAILOR_APP_AUTOACCEPT_H_
