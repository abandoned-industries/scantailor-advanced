// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AppleVisionDetector.h"

#include <QtGlobal>  // Must include before checking Q_OS_MACOS

#ifdef Q_OS_MACOS

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <AppKit/AppKit.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <QImageReader>
#include <QElapsedTimer>

namespace {

void releaseQImageData(void* info, const void*, size_t) {
  delete static_cast<QImage*>(info);
}

// Convert QImage to CGImage for Vision framework.  The provider retains a
// heap-held QImage value, whose implicit sharing keeps the pixel buffer alive
// without copying compatible ARGB data.
CGImageRef createCGImageFromQImage(const QImage& qimage) {
  QImage img = qimage;
  const bool grayscaleExperiment =
      qEnvironmentVariableIntValue("SCANTAILOR_VISION_GRAYSCALE8") != 0;
  bool grayscale = grayscaleExperiment && img.format() == QImage::Format_Grayscale8;
  if (!grayscale
      && grayscaleExperiment
      && (img.format() == QImage::Format_Mono
          || img.format() == QImage::Format_MonoLSB
          || (img.format() == QImage::Format_Indexed8 && img.isGrayscale()))) {
    img = img.convertToFormat(QImage::Format_Grayscale8);
    grayscale = true;
  } else if (!grayscale
             && img.format() != QImage::Format_RGB32
             && img.format() != QImage::Format_ARGB32
             && img.format() != QImage::Format_ARGB32_Premultiplied) {
    img = img.convertToFormat(QImage::Format_ARGB32);
  }

  CGColorSpaceRef colorSpace = grayscale
      ? CGColorSpaceCreateDeviceGray()
      : CGColorSpaceCreateDeviceRGB();
  if (!colorSpace) {
    return nullptr;
  }

  auto* retainedImage = new QImage(img);
  CGDataProviderRef dataProvider = CGDataProviderCreateWithData(
      retainedImage, retainedImage->constBits(), retainedImage->sizeInBytes(), releaseQImageData);
  if (!dataProvider) {
    delete retainedImage;
    CGColorSpaceRelease(colorSpace);
    return nullptr;
  }

  CGImageRef cgImage = CGImageCreate(
      img.width(),
      img.height(),
      8,                          // bits per component
      grayscale ? 8 : 32,        // bits per pixel
      img.bytesPerLine(),
      colorSpace,
      grayscale
          ? kCGImageAlphaNone
          : ((img.format() == QImage::Format_ARGB32_Premultiplied
                  ? kCGImageAlphaPremultipliedFirst
                  : (img.format() == QImage::Format_RGB32 ? kCGImageAlphaNoneSkipFirst : kCGImageAlphaFirst))
             | kCGBitmapByteOrder32Host),
      dataProvider,
      nullptr,                    // decode array
      false,                      // should interpolate
      kCGRenderingIntentDefault);

  // CGImageCreate retains both colorSpace and dataProvider internally,
  // so we must release our references here
  CGDataProviderRelease(dataProvider);
  CGColorSpaceRelease(colorSpace);

  return cgImage;
}

// Perform Vision request synchronously
NSArray* performVisionRequest(VNRequest* request, CGImageRef cgImage, bool* success = nullptr) {
  if (success) {
    *success = false;
  }
  VNImageRequestHandler* handler = [[VNImageRequestHandler alloc]
      initWithCGImage:cgImage
              options:@{}];

  NSError* error = nil;
  [handler performRequests:@[request] error:&error];

  if (error) {
    NSLog(@"Vision request failed: %@", error.localizedDescription);
    return nil;
  }

  if (success) {
    *success = true;
  }
  return request.results;
}

NSArray<NSDictionary<NSString*, id>*>* performDocumentOcr(
    CGImageRef image,
    const AppleVisionDetector::OcrConfig& config,
    bool* success) {
  if (success) {
    *success = false;
  }
  // The document recognizer is faster per page and slightly more accurate, but
  // macOS 26 crashes when its requests overlap in one process. Serializing it
  // makes a full book slower than Vision's stable page-parallel text request.
  // Keep it as an explicit experiment until it can run in isolated helpers.
  if (qEnvironmentVariableIntValue("SCANTAILOR_DOCUMENT_VISION_OCR") == 0
      || !qEnvironmentVariable("SCANTAILOR_OCR_MIN_TEXT_HEIGHT").isEmpty()) {
    return nil;
  }

  if (@available(macOS 26.0, *)) {
    static Class bridgeClass = Nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
      NSString* frameworksPath = NSBundle.mainBundle.privateFrameworksPath;
      NSString* bridgePath =
          [frameworksPath stringByAppendingPathComponent:@"libSpectreDocumentOCRBridge.dylib"];
      if (dlopen(bridgePath.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL)) {
        bridgeClass = NSClassFromString(@"SpectreDocumentOCRBridge");
      }
    });

    SEL selector =
        NSSelectorFromString(@"recognizeImage:languageCode:usesLanguageCorrection:error:");
    if (!bridgeClass || ![bridgeClass respondsToSelector:selector]) {
      return nil;
    }

    using Recognize = NSArray* (*)(id, SEL, CGImageRef, NSString*, BOOL, NSError**);
    auto recognize = reinterpret_cast<Recognize>(objc_msgSend);
    NSError* error = nil;
    NSString* languageCode =
        config.languageCode.isEmpty() ? nil : config.languageCode.toNSString();
    bool correctionOverrideOk = false;
    const int correctionOverride =
        qEnvironmentVariableIntValue("SCANTAILOR_OCR_LANGUAGE_CORRECTION", &correctionOverrideOk);
    const bool usesLanguageCorrection =
        correctionOverrideOk ? correctionOverride != 0 : config.usesLanguageCorrection;
    NSArray* lines = recognize(
        bridgeClass,
        selector,
        image,
        languageCode,
        usesLanguageCorrection,
        &error);
    if (!lines) {
      qWarning() << "OCR: Document recognizer failed; using legacy Vision:"
                 << QString::fromNSString(error.localizedDescription);
      return nil;
    }
    if (success) {
      *success = true;
    }
    return lines;
  }
  return nil;
}

}  // namespace

bool AppleVisionDetector::isAvailable() {
  // Vision framework is available on macOS 10.13+
  if (@available(macOS 10.15, *)) {
    return true;
  }
  return false;
}

QVector<AppleVisionDetector::TextRegion> AppleVisionDetector::detectTextRegions(const QImage& image) {
  QVector<TextRegion> regions;

  if (!isAvailable() || image.isNull()) {
    return regions;
  }

  @autoreleasepool {
    CGImageRef cgImage = createCGImageFromQImage(image);
    if (!cgImage) {
      return regions;
    }

    if (@available(macOS 10.15, *)) {
      VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];
      request.recognitionLevel = VNRequestTextRecognitionLevelFast;
      request.usesLanguageCorrection = NO;  // Faster without correction

      NSArray* results = performVisionRequest(request, cgImage);
      CGImageRelease(cgImage);

      qreal imgWidth = image.width();
      qreal imgHeight = image.height();

      for (VNRecognizedTextObservation* observation in results) {
        TextRegion region;

        CGRect bbox = observation.boundingBox;
        region.bounds = QRectF(
            bbox.origin.x * imgWidth,
            (1.0 - bbox.origin.y - bbox.size.height) * imgHeight,
            bbox.size.width * imgWidth,
            bbox.size.height * imgHeight);

        region.confidence = observation.confidence;

        // Get the recognized text
        NSArray<VNRecognizedText*>* candidates = [observation topCandidates:1];
        if (candidates.count > 0) {
          region.text = QString::fromNSString(candidates.firstObject.string);
        }

        regions.append(region);
      }
    }
  }

  return regions;
}

QVector<AppleVisionDetector::RectangleResult> AppleVisionDetector::detectRectangles(
    const QImage& image) {
  QVector<RectangleResult> rectangles;
  if (!isAvailable() || image.isNull()) {
    return rectangles;
  }

  @autoreleasepool {
    CGImageRef cgImage = createCGImageFromQImage(image);
    if (!cgImage) {
      return rectangles;
    }

    VNDetectRectanglesRequest* request = [[VNDetectRectanglesRequest alloc] init];
    request.maximumObservations = 32;
    request.minimumSize = 0.06f;
    request.minimumAspectRatio = 0.12f;
    request.maximumAspectRatio = 1.0f;
    request.quadratureTolerance = 12.0f;
    request.minimumConfidence = 0.25f;

    NSArray* observations = performVisionRequest(request, cgImage);
    CGImageRelease(cgImage);
    const qreal width = image.width();
    const qreal height = image.height();
    for (VNRectangleObservation* observation in observations) {
      const CGRect box = observation.boundingBox;
      RectangleResult result;
      result.bounds = QRectF(box.origin.x * width,
                             (1.0 - box.origin.y - box.size.height) * height,
                             box.size.width * width,
                             box.size.height * height);
      result.confidence = observation.confidence;
      if (!result.bounds.isEmpty()) {
        rectangles.push_back(result);
      }
    }
  }
  return rectangles;
}

AppleVisionDetector::PageSplitResult AppleVisionDetector::detectPageSplit(const QImage& image) {
  PageSplitResult result;
  result.shouldSplit = false;
  result.splitLineX = 0.5;
  result.confidence = 0.0f;
  result.leftTextRegions = 0;
  result.rightTextRegions = 0;
  result.hasAnyPageNumber = false;
  result.hasPageNumbers = false;
  result.rightmostLeftTextX = 0.0;

  if (!isAvailable() || image.isNull()) {
    return result;
  }

  // Guard against division by zero
  if (image.width() == 0 || image.height() == 0) {
    qDebug() << "PageSplit: image has zero dimensions";
    return result;
  }

  // Check aspect ratio - but use a lower threshold to allow analysis
  // We'll use other signals (like page numbers) to validate
  const qreal aspectRatio = static_cast<qreal>(image.width()) / image.height();
  if (aspectRatio < 0.85) {
    // Clearly portrait - definitely not a two-page spread
    qDebug() << "PageSplit: aspect ratio" << aspectRatio << "too narrow for two-page spread (need > 0.85)";
    return result;
  }

  qDebug() << "PageSplit: analyzing image with aspect ratio" << aspectRatio;

  // Detect text regions
  QVector<TextRegion> regions = detectTextRegions(image);
  result.textRegions = regions;

  if (regions.isEmpty()) {
    qDebug() << "PageSplit: no text regions detected";
    return result;
  }

  qDebug() << "PageSplit: detected" << regions.size() << "text regions";

  const qreal imageWidth = image.width();
  const qreal imageHeight = image.height();

  // SMART FEATURE: Page number detection
  // Look for small text regions in bottom corners that contain numbers
  // This is a very strong signal of a two-page spread
  bool hasLeftPageNumber = false;
  bool hasRightPageNumber = false;
  QString leftPageNum, rightPageNum;

  // Define corner regions for page numbers (bottom 15% of page, outer 25% on each side)
  const qreal bottomThreshold = imageHeight * 0.85;
  const qreal leftCornerEnd = imageWidth * 0.25;
  const qreal rightCornerStart = imageWidth * 0.75;

  // Also check for page numbers near the center-bottom (common in some books)
  const qreal centerLeftStart = imageWidth * 0.35;
  const qreal centerLeftEnd = imageWidth * 0.48;
  const qreal centerRightStart = imageWidth * 0.52;
  const qreal centerRightEnd = imageWidth * 0.65;

  for (const TextRegion& region : regions) {
    // Check if region is in the bottom area
    if (region.bounds.top() < bottomThreshold) continue;

    // Check if the text looks like a page number (short, contains digits)
    const QString text = region.text.trimmed();
    if (text.isEmpty() || text.length() > 10) continue;

    // Check if it contains at least one digit
    bool hasDigit = false;
    for (const QChar& c : text) {
      if (c.isDigit()) {
        hasDigit = true;
        break;
      }
    }
    if (!hasDigit) continue;

    const qreal regionCenterX = region.bounds.center().x();

    // Check left side (outer corner or center-left)
    if ((regionCenterX < leftCornerEnd) ||
        (regionCenterX >= centerLeftStart && regionCenterX < centerLeftEnd)) {
      hasLeftPageNumber = true;
      leftPageNum = text;
      qDebug() << "PageSplit: found left page number candidate:" << text << "at x:" << regionCenterX;
    }
    // Check right side (outer corner or center-right)
    else if ((regionCenterX > rightCornerStart) ||
             (regionCenterX >= centerRightStart && regionCenterX <= centerRightEnd)) {
      hasRightPageNumber = true;
      rightPageNum = text;
      qDebug() << "PageSplit: found right page number candidate:" << text << "at x:" << regionCenterX;
    }
  }

  // If we found page numbers on both sides, that's a very strong signal
  const bool hasPageNumbers = hasLeftPageNumber && hasRightPageNumber;
  result.hasAnyPageNumber = hasLeftPageNumber || hasRightPageNumber;
  result.hasPageNumbers = hasPageNumbers;
  if (hasPageNumbers) {
    qDebug() << "PageSplit: STRONG SIGNAL - page numbers detected on both sides:"
             << leftPageNum << "/" << rightPageNum;
  }

  // Analyze horizontal distribution of text regions using zone-based clustering
  // The key insight: two-page spreads have text clustered in left and right zones
  // with relatively empty center (the gutter), while single pages with columns
  // have text distributed more evenly or in different patterns.

  // Define zones: left page (0%-48%), gutter (48%-52%), right page (52%-100%)
  // Wider page zones and narrower gutter to catch more text regions
  const qreal leftZoneEnd = imageWidth * 0.48;
  const qreal rightZoneStart = imageWidth * 0.52;
  const qreal gutterStart = imageWidth * 0.48;
  const qreal gutterEnd = imageWidth * 0.52;

  // Count regions in each zone and track rightmost left-zone text boundary
  int leftCount = 0;
  int rightCount = 0;
  int gutterCount = 0;
  qreal rightmostLeftX = 0.0;

  for (const TextRegion& region : regions) {
    const qreal regionCenterX = region.bounds.center().x();

    if (regionCenterX < leftZoneEnd) {
      leftCount++;
      rightmostLeftX = qMax(rightmostLeftX, region.bounds.right());
    } else if (regionCenterX >= rightZoneStart) {
      rightCount++;
    } else if (regionCenterX >= gutterStart && regionCenterX <= gutterEnd) {
      gutterCount++;
    }
  }

  result.leftTextRegions = leftCount;
  result.rightTextRegions = rightCount;
  result.rightmostLeftTextX = (leftCount > 0 && imageWidth > 0)
      ? rightmostLeftX / imageWidth : 0.0;

  qDebug() << "PageSplit: left zone regions:" << leftCount
           << "gutter regions:" << gutterCount
           << "right zone regions:" << rightCount;

  // If we have page numbers on both sides, use relaxed criteria
  // Otherwise, use stricter criteria
  int minRegionsPerSide = hasPageNumbers ? 1 : 3;
  qreal maxGutterRatio = hasPageNumbers ? 0.25 : 0.15;
  qreal minBalance = hasPageNumbers ? 0.05 : 0.1;
  float minConfidence = hasPageNumbers ? 0.40f : 0.60f;

  // Determine if this looks like a two-page spread
  if (leftCount < minRegionsPerSide || rightCount < minRegionsPerSide) {
    qDebug() << "PageSplit: not enough regions on both sides (need" << minRegionsPerSide << "each)";
    return result;
  }

  // The gutter should be relatively empty compared to the page zones
  // If there's too much content in the gutter, it's probably a single page
  const int totalSideRegions = leftCount + rightCount;
  const qreal gutterRatio = static_cast<qreal>(gutterCount) / (totalSideRegions + gutterCount);
  if (gutterRatio > maxGutterRatio) {
    qDebug() << "PageSplit: too much content in gutter zone:" << (gutterRatio * 100) << "% (max:" << (maxGutterRatio * 100) << "%)";
    return result;
  }

  // The regions should have some balance between left and right
  // But we use a low threshold because one page might be mostly graphics
  const qreal balance = static_cast<qreal>(qMin(leftCount, rightCount)) / qMax(leftCount, rightCount);
  if (balance < minBalance) {
    qDebug() << "PageSplit: extremely unbalanced sides, ratio:" << balance << "(min:" << minBalance << ")";
    return result;
  }

  // Calculate confidence based on various factors
  float confidence = 0.0f;

  // Factor 1: Balance of content (more balanced = higher confidence)
  confidence += static_cast<float>(balance) * 0.20f;

  // Factor 2: Empty gutter (less content in gutter = higher confidence)
  const float gutterEmptiness = 1.0f - static_cast<float>(gutterRatio) / maxGutterRatio;
  confidence += gutterEmptiness * 0.20f;

  // Factor 3: Number of text regions (more regions = more confident in the analysis)
  const float regionFactor = qMin(totalSideRegions / 20.0f, 1.0f);  // Max out at 20 regions
  confidence += regionFactor * 0.20f;

  // Factor 4: Aspect ratio (wider images are more likely to be spreads)
  const float aspectFactor = qMin(qMax((static_cast<float>(aspectRatio) - 0.85f) / 0.5f, 0.0f), 1.0f);
  confidence += aspectFactor * 0.20f;

  // Factor 5: Page numbers (strong signal!)
  const float pageNumberFactor = hasPageNumbers ? 1.0f : 0.0f;
  confidence += pageNumberFactor * 0.20f;

  qDebug() << "PageSplit: confidence factors - balance:" << balance
           << "gutterEmpty:" << gutterEmptiness
           << "regions:" << regionFactor
           << "aspect:" << aspectFactor
           << "pageNumbers:" << pageNumberFactor
           << "total:" << confidence;

  // Check minimum confidence
  if (confidence < minConfidence) {
    qDebug() << "PageSplit: confidence too low:" << confidence << "(min:" << minConfidence << ")";
    return result;
  }

  // Split at the center of the image (the gutter)
  result.splitLineX = 0.5;  // Always split at center for two-page spreads
  result.shouldSplit = true;
  result.confidence = confidence;

  qDebug() << "PageSplit: detected two-page spread, split at:" << result.splitLineX
           << "confidence:" << confidence;

  return result;
}

AppleVisionDetector::PageSplitResult AppleVisionDetector::detectPageSplitFromFile(const QString& imagePath) {
  QImageReader reader(imagePath);
  if (!reader.canRead()) {
    PageSplitResult result;
    result.shouldSplit = false;
    result.splitLineX = 0.5;
    result.confidence = 0.0f;
    result.leftTextRegions = 0;
    result.rightTextRegions = 0;
    return result;
  }

  // Scale down for faster analysis
  const QSize originalSize = reader.size();
  const int maxDim = 1500;  // Good resolution for text detection
  if (originalSize.width() > maxDim || originalSize.height() > maxDim) {
    const qreal scale = qMin(static_cast<qreal>(maxDim) / originalSize.width(),
                             static_cast<qreal>(maxDim) / originalSize.height());
    reader.setScaledSize(QSize(qRound(originalSize.width() * scale),
                               qRound(originalSize.height() * scale)));
  }

  const QImage image = reader.read();
  return detectPageSplit(image);
}

QVector<AppleVisionDetector::OcrWordResult> AppleVisionDetector::performOcr(const QImage& image,
                                                                             const OcrConfig& config,
                                                                             bool* success) {
  QVector<OcrWordResult> results;
  if (success) {
    *success = false;
  }

  if (!isAvailable() || image.isNull()) {
    return results;
  }

  @autoreleasepool {
    QImage visionImage = image;
    bool maximumDimensionOk = false;
    const int maximumDimension =
        qEnvironmentVariableIntValue("SCANTAILOR_OCR_MAX_DIM", &maximumDimensionOk);
    if (maximumDimensionOk
        && maximumDimension > 0
        && std::max(image.width(), image.height()) > maximumDimension) {
      visionImage = image.scaled(
          maximumDimension,
          maximumDimension,
          Qt::KeepAspectRatio,
          Qt::SmoothTransformation);
    }

    CGImageRef cgImage = createCGImageFromQImage(visionImage);
    if (!cgImage) {
      return results;
    }

    if (@available(macOS 10.15, *)) {
      QElapsedTimer visionTimer;
      visionTimer.start();
      bool documentOcrSuccess = false;
      NSArray<NSDictionary<NSString*, id>*>* documentLines =
          performDocumentOcr(cgImage, config, &documentOcrSuccess);
      if (documentOcrSuccess) {
        qDebug() << "OCR: Document request completed in" << visionTimer.elapsed() << "ms";
        CGImageRelease(cgImage);

        const qreal imgWidth = image.width();
        const qreal imgHeight = image.height();
        for (NSDictionary<NSString*, id>* line in documentLines) {
          OcrWordResult word;
          word.text = QString::fromNSString(line[@"text"]);
          word.confidence = [line[@"confidence"] floatValue];
          const qreal x = [line[@"x"] doubleValue];
          const qreal y = [line[@"y"] doubleValue];
          const qreal width = [line[@"width"] doubleValue];
          const qreal height = [line[@"height"] doubleValue];
          word.bounds = QRectF(
              x * imgWidth,
              (1.0 - y - height) * imgHeight,
              width * imgWidth,
              height * imgHeight);
          results.append(word);
        }
        if (success) {
          *success = true;
        }
        return results;
      }

      VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];

      // Set recognition level
      request.recognitionLevel = config.useAccurateRecognition
                                     ? VNRequestTextRecognitionLevelAccurate
                                     : VNRequestTextRecognitionLevelFast;

      bool correctionOverrideOk = false;
      const int correctionOverride =
          qEnvironmentVariableIntValue("SCANTAILOR_OCR_LANGUAGE_CORRECTION", &correctionOverrideOk);
      request.usesLanguageCorrection =
          correctionOverrideOk ? correctionOverride != 0 : config.usesLanguageCorrection;

      bool minimumHeightOk = false;
      const float minimumTextHeight =
          qEnvironmentVariable("SCANTAILOR_OCR_MIN_TEXT_HEIGHT").toFloat(&minimumHeightOk);
      if (minimumHeightOk && minimumTextHeight >= 0.0f && minimumTextHeight <= 1.0f) {
        request.minimumTextHeight = minimumTextHeight;
      }

      // A saved language can outlive the Vision revision or macOS version that
      // supported it. Use it only when this exact request accepts it; otherwise
      // preserve OCR by falling back to automatic detection.
      if (config.languageCode == QStringLiteral("auto")) {
        if (@available(macOS 13.0, *)) {
          request.automaticallyDetectsLanguage = YES;
        }
      } else if (!config.languageCode.isEmpty()) {
        NSString* langCode = config.languageCode.toNSString();
        NSError* languageError = nil;
        NSArray<NSString*>* supportedLanguages =
            [request supportedRecognitionLanguagesAndReturnError:&languageError];
        if ([supportedLanguages containsObject:langCode]) {
          request.recognitionLanguages = @[ langCode ];
        } else if (@available(macOS 13.0, *)) {
          request.automaticallyDetectsLanguage = YES;
          qWarning() << "OCR: Saved language is unsupported by this Vision request; using Auto:"
                     << config.languageCode;
        }
      }

      visionTimer.restart();
      NSArray* observations = performVisionRequest(request, cgImage, success);
      qDebug() << "OCR: Vision request completed in" << visionTimer.elapsed() << "ms";
      CGImageRelease(cgImage);

      qreal imgWidth = image.width();
      qreal imgHeight = image.height();

      for (VNRecognizedTextObservation* observation in observations) {
        NSArray<VNRecognizedText*>* candidates = [observation topCandidates:1];
        if (candidates.count == 0) {
          continue;
        }

        VNRecognizedText* bestCandidate = candidates.firstObject;
        NSString* fullText = bestCandidate.string;

        // Get the bounding box for the entire text line/block
        CGRect bbox = observation.boundingBox;

        OcrWordResult word;
        word.text = QString::fromNSString(fullText);
        word.confidence = observation.confidence;

        // Convert Vision coordinates (normalized, Y-inverted) to image coords
        // Vision: origin at bottom-left, normalized 0-1
        // Image: origin at top-left, in pixels
        word.bounds = QRectF(bbox.origin.x * imgWidth,
                             (1.0 - bbox.origin.y - bbox.size.height) * imgHeight,
                             bbox.size.width * imgWidth,
                             bbox.size.height * imgHeight);

        results.append(word);
      }
    }
  }

  return results;
}

QStringList AppleVisionDetector::supportedOcrLanguages() {
  QStringList languages;

  if (@available(macOS 10.15, *)) {
    @autoreleasepool {
      VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] init];
      request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
      NSError* error = nil;
      NSArray<NSString*>* supportedLanguages =
          [request supportedRecognitionLanguagesAndReturnError:&error];
      for (NSString* language in supportedLanguages) {
        languages.append(QString::fromNSString(language));
      }
    }
  }

  return languages;
}

#endif  // Q_OS_MACOS
