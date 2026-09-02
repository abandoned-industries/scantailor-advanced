// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OCR_OCRTEXTCOLLECTOR_H_
#define SCANTAILOR_OCR_OCRTEXTCOLLECTOR_H_

#include <QMap>
#include <QString>

#include "PdfExporter.h"

class PageSequence;
class OutputFileNameGenerator;

namespace ocr {
class Settings;

/**
 * \brief Gathers the OCR text layer for PDF export, keyed by output file path.
 *
 * A page contributes text when its output file exists and a non-empty OCR
 * result is stored for it. Whether OCR is currently enabled as a pipeline
 * stage is deliberately not consulted: stored results are the ground truth,
 * so text recognized in an earlier run (e.g. Auto Process) is never silently
 * dropped from the exported PDF.
 */
QMap<QString, PdfExporter::OcrTextData> collectOcrTextData(const PageSequence& pages,
                                                           const OutputFileNameGenerator& outFileNameGen,
                                                           const Settings& settings);

}  // namespace ocr

#endif  // SCANTAILOR_OCR_OCRTEXTCOLLECTOR_H_
