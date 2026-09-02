// Copyright (C) 2026  ScanTailor Spectre contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OcrTextCollector.h"

#include <QFile>
#include <memory>

#include "OcrResult.h"
#include "OutputFileNameGenerator.h"
#include "PageSequence.h"
#include "Settings.h"

namespace ocr {

QMap<QString, PdfExporter::OcrTextData> collectOcrTextData(const PageSequence& pages,
                                                           const OutputFileNameGenerator& outFileNameGen,
                                                           const Settings& settings) {
  QMap<QString, PdfExporter::OcrTextData> ocrData;
  for (const PageInfo& pageInfo : pages) {
    const QString filePath = outFileNameGen.filePathFor(pageInfo.id());
    if (!QFile::exists(filePath)) {
      continue;  // Skip unprocessed pages
    }

    const std::unique_ptr<OcrResult> result = settings.getOcrResult(pageInfo.id());
    if (result && !result->isEmpty()) {
      PdfExporter::OcrTextData textData;
      textData.imageWidth = result->imageWidth();
      textData.imageHeight = result->imageHeight();

      for (const OcrWord& word : result->words()) {
        PdfExporter::OcrTextData::Word pdfWord;
        pdfWord.text = word.text;
        pdfWord.bounds = word.boundingBox;
        textData.words.append(pdfWord);
      }

      ocrData.insert(filePath, textData);
    }
  }
  return ocrData;
}

}  // namespace ocr
