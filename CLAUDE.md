# CLAUDE.md - ScanTailor Spectre Architecture

**macOS only** - This application requires macOS 11+ and uses Apple-specific frameworks (CoreGraphics, Vision, Metal).

> **⚠️ ALWAYS read `~/Developer/CLAUDE.md` FIRST** (cross-project conventions for all repos under `~/Developer` — canonical build location, the required repo-root app symlink, stray-build-dir rules). Those rules apply here and override nothing in this file; this file only adds project specifics.

---

## ⚠️ SESSION STARTUP — READ THIS FIRST

**EVERY session start AND after EVERY compaction, do these IMMEDIATELY:**

```bash
cat ~/Developer/CLAUDE.md   # Cross-project conventions (build location, repo-root app symlink, strays)
cat TODO.md           # Current task and acceptance criteria
tail -100 BUILD_LOG.md # Recent builds, errors, and known issues
```

---

## 🚨 PRIME DIRECTIVE 🚨

**BEFORE running `make` or `cmake --build`, ALWAYS append to BUILD_LOG.md:**
```bash
cat >> BUILD_LOG.md << 'EOF'

---
YYYY-MM-DD HH:MM - Description of changes (make -j8)
- File changed: what was done
- File changed: what was done
EOF
```

**NO EXCEPTIONS. Log first, build second.**

---

**CRITICAL RULES — These persist across compaction:**

1. **PRIME DIRECTIVE**: Log to BUILD_LOG.md BEFORE every build (see above)
2. **Version/Timestamp**: ALL builds must be timestamped: `ScanTailor-Spectre-X.Y.Z-YYYYMMDD-HHMM.dmg`
3. **README/PDF**: Any version increment requires: update README.md, regenerate PDF, include new PDF in DMG
4. **Task Tracking**: Check off completed tasks in `TODO.md` with timestamp

**Current version**: Check `CMakeLists.txt` line containing `VERSION` or `project()`

---

## Companion Files (MUST READ)

| File | Purpose | When to Read |
|------|---------|--------------|
| `TODO.md` | Current task, upcoming work | Session start, after compaction |
| `BUILD_LOG.md` | Build history, known failures, lessons learned | Before any build, after errors |

---

## Overview

ScanTailor Spectre transforms raw scans into clean, publication-ready PDFs through a 10-stage processing pipeline. Fork of ScanTailor Advanced with PDF import/export, intelligent color detection, multi-select batch processing, and native Apple Silicon support.

## Directory Structure

```
src/
├── app/                    # Main application (MainWindow, dialogs, startup)
├── core/                   # Core processing logic
│   └── filters/            # The 10 filter stages (see below)
├── imageproc/              # Image processing algorithms (binarize, despeckle, etc.)
├── dewarping/              # Book page flattening algorithms
├── math/                   # Splines, matrices, linear algebra
├── foundation/             # Utilities, data structures
└── acceleration/           # Metal GPU shaders (macOS)
```

## The 10 Filter Stages

Each filter lives in `src/core/filters/{stage}/` with consistent structure:

| Stage | Directory | Purpose |
|-------|-----------|---------|
| 1 | `fix_orientation/` | Rotate pages (90° increments) |
| 2 | `page_split/` | Separate two-page spreads |
| 3 | `deskew/` | Straighten tilted scans |
| 4 | `page_box/` | Define page boundaries |
| 5 | `select_content/` | Define content boundaries |
| 6 | `page_layout/` | Set margins and alignment |
| 7 | `finalize/` | Choose color mode (B&W/Gray/Color) |
| 8 | `output/` | Apply image processing (binarize, despeckle, dewarp) |
| 9 | `ocr/` | OCR text recognition for searchable PDF |
| 10 | `export_/` | Create final PDF |

### Filter Architecture Pattern

Every filter follows the same structure:
```
filters/{stage}/
├── Filter.cpp/h          # Main filter class (extends AbstractFilter)
├── Task.cpp/h            # Background processing task
├── CacheDrivenTask.cpp/h # Fast path using cached results
├── Settings.cpp/h        # Parameter storage
├── OptionsWidget.cpp/h   # Qt UI controls
├── ImageView.cpp/h       # Image display with overlays
└── Thumbnail.cpp/h       # Preview for thumbnail strip
```

## Key Classes

### Core Infrastructure
- **AbstractFilter** (`src/core/AbstractFilter.h`) - Base class for all 8 filters
- **StageSequence** (`src/core/StageSequence.h`) - Orchestrates filter pipeline
- **ProjectPages** (`src/core/ProjectPages.h`) - Manages image collection
- **BackgroundExecutor** (`src/core/BackgroundExecutor.h`) - Thread pool for async tasks
- **MainWindow** (`src/app/MainWindow.cpp`) - Main UI, implements FilterUiInterface

### Image Processing
- **Binarize** (`src/imageproc/Binarize.h`) - B&W conversion (Sauvola, global threshold)
- **Despeckle** (`src/core/Despeckle.h`) - Noise removal
- **WhiteBalance** (`src/core/WhiteBalance.h`) - Color correction
- **CylindricalSurfaceDewarper** (`src/dewarping/`) - Page flattening

### I/O
- **PdfReader** (`src/core/PdfReader.mm`) - PDF import using CoreGraphics (macOS only)
- **PdfExporter** (`src/core/PdfExporter.mm`) - PDF export via CoreGraphics CGPDFContext
- **ProjectReader/Writer** (`src/core/`) - .ScanTailor project files (XML)

### Apple-Specific
- **AppleVisionDetector** (`src/core/AppleVisionDetector.mm`) - Vision framework for page split detection and OCR
- **MetalContext/MetalGaussBlur/MetalMorphology** (`src/acceleration/`) - GPU acceleration (currently disabled due to background app crashes)

## Processing Flow

```
Input (PDF/Images)
    ↓
ProjectPages (image collection)
    ↓
Stage 1-4: Geometry corrections (orientation, split, deskew, page box)
    ↓
Stage 5-6: Content selection and margins
    ↓
Stage 7: Color mode decision (Leptonica-based detection)
    ↓
Stage 8: Image processing → TIFF/PNG output
    ↓
Stage 9: OCR text layer (Apple Vision framework)
    ↓
Stage 10: PdfExporter → Final PDF
```

Each page flows through filters independently. Results are cached in project folder.

**Multi-select batch processing**: Hold Cmd/Ctrl to select multiple pages, then apply settings to all selected pages at once.

## Coordinate Systems (ImageViewBase)

The image view manages transforms between:
1. **Image coords** - Original pixels
2. **Virtual coords** - After geometric transforms
3. **Widget coords** - Screen pixels

Key transforms: `m_imageToVirtual`, `m_virtualToWidget`

## Build Commands

**REMEMBER: Log to BUILD_LOG.md BEFORE every build with timestamp and changes!**

```bash
# Install dependencies
brew install qt6 boost libtiff libpng jpeg cmake libharu leptonica

# Build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 ..
cmake --build . --target scantailor -j$(sysctl -n hw.ncpu)  # fast incremental app build
cmake --build . --target scantailor_bundle                # explicit bundle/deploy/sign step

# Bundle Qt frameworks manually only if needed
macdeployqt "ScanTailor Spectre.app" -always-overwrite

# ⚠️ NEVER run macdeployqt on the app inside build/ — deploy only a COPY staged
# outside build/ (e.g. release-staging/). A deployed bundle in build/ breaks every
# later incremental dev build: the fresh executable links Homebrew Qt while the
# bundle's stale qt.conf/PlugIns force the old bundled cocoa plugin, and the app
# aborts at launch (qt.qpa.plugin cocoa error; bitten 2026-07-23 and 2026-07-25).
# Cure if it happens: rm -rf Contents/{Frameworks,PlugIns,Resources/qt.conf,Resources/qml}
# from the build/ app.

# RELEASE PREP — Update README and generate PDF for DMG
# 1. Update README.md with version number, date, and changelog
# 2. Generate PDF:
pandoc README.md -o README.pdf --pdf-engine=wkhtmltopdf
# 3. Copy into app bundle (or staging folder for DMG):
cp README.pdf "ScanTailor Spectre.app/Contents/Resources/"
# NOTE: Do this BEFORE signing — modifying the bundle after signing invalidates it!

# Sign - must sign components individually, --deep doesn't work for Qt frameworks
IDENTITY="${STS_CODESIGN_IDENTITY}"
APP="ScanTailor Spectre.app"

# 1. Sign framework binaries inside Versions/A
find "$APP/Contents/Frameworks" -type f -name "Qt*" -path "*/Versions/A/*" | while read bin; do
  codesign --force --options runtime --sign "$IDENTITY" "$bin"
done

# 2. Sign frameworks
find "$APP/Contents/Frameworks" -name "*.framework" -type d | while read fw; do
  codesign --force --options runtime --sign "$IDENTITY" "$fw"
done

# 3. Sign dylibs
find "$APP/Contents/Frameworks" -name "*.dylib" -type f | while read lib; do
  codesign --force --options runtime --sign "$IDENTITY" "$lib"
done

# 4. Sign plugins
find "$APP/Contents/PlugIns" -name "*.dylib" -type f | while read plugin; do
  codesign --force --options runtime --sign "$IDENTITY" "$plugin"
done

# 5. Sign main executable and app bundle
codesign --force --options runtime --sign "$IDENTITY" "$APP/Contents/MacOS/ScanTailor Spectre"
codesign --force --options runtime --sign "$IDENTITY" "$APP"

# 6. Verify signature
codesign --verify --deep --strict "$APP"

# 7. Create zip with ditto (MUST use ditto, not zip - preserves symlinks correctly)
ditto -c -k --keepParent "$APP" app.zip

# 8. Notarize
xcrun notarytool submit app.zip --keychain-profile "notary" --wait

# 9. Staple
xcrun stapler staple "$APP"

# Create DMG with app and README PDF
mkdir -p dmg-staging
cp -R "ScanTailor Spectre.app" dmg-staging/
cp "ScanTailor Spectre Readme.pdf" dmg-staging/

VERSION=$(grep -o 'VERSION [0-9.]*' CMakeLists.txt | awk '{print $2}')
TIMESTAMP=$(date +%Y%m%d-%H%M)
hdiutil create -volname "ScanTailor Spectre" -srcfolder dmg-staging \
    -ov -format UDZO ScanTailor-Spectre-${VERSION}-${TIMESTAMP}.dmg

rm -rf dmg-staging
```

## Build Artifacts

- **ALWAYS timestamp DMG builds**: `ScanTailor-Spectre-X.Y.Z-YYYYMMDD-HHMM.dmg`

## Push New Release

When user says "push new release", do ALL of these steps:

1. **Bump version** in `version.h.in` (e.g., 2.0a17 → 2.0a18)
2. **Update README.md** version if needed
3. **Log to BUILD_LOG.md** before building
4. **Rebuild**: `cd build && cmake --build . -j$(sysctl -n hw.ncpu)`
5. **Generate README PDF** (see PDF Generation section below)
6. **Sign the app** with Developer ID (identity: "Developer ID Application: Kazys Varnelis (PHCL25Z99X)")
7. **Notarize**: `ditto -c -k --keepParent "$APP" app.zip && xcrun notarytool submit app.zip --keychain-profile "notary" --wait`
8. **Staple**: `xcrun stapler staple "$APP"`
9. **Create DMG** with app + README PDF, timestamped filename
10. **Commit** version bump and any pending changes
11. **Tag** with version (e.g., `git tag v2.0a18`)
12. **Push**: `git push origin main --tags`

## PDF Generation from README

To create the README PDF for distribution:

```bash
# Generate HTML with styling:
# - Replace GitHub image URL with local file, sized to 128px
# - Add page breaks before "Quick Start" and "Credits" sections
# - Use class="no-rule" to suppress horizontal rule on page-break sections
cat README.md | \
  sed 's|<img.*/>|<img src="src/resources/scantailor-spectre2.png" width="128" alt="ScanTailor Spectre"/>|' | \
  sed 's|## Quick Start|<div style="page-break-before: always"></div>\n\n## Quick Start {.no-rule}|' | \
  sed 's|## Credits|<div style="page-break-before: always"></div>\n\n## Credits {.no-rule}|' \
  > README_temp.md
pandoc README_temp.md -t html --standalone > README.html

# Add CSS to suppress border on .no-rule headings (insert after opening <style> tag)
sed -i '' 's|<style>|<style>\n    h2.no-rule { border-bottom: none !important; }|' README.html

# Generate PDF using Chrome headless
/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome \
  --headless --disable-gpu \
  --print-to-pdf="ScanTailor Spectre Readme.pdf" \
  --no-pdf-header-footer README.html

# Clean up
rm README_temp.md
```

## Timestamp Display in About Menu

The timestamp is displayed in the About dialog through this flow:

### 1. CMake Generates the Timestamp at Build Time

`cmake/GenerateVersionH.cmake`:
```cmake
string(TIMESTAMP BUILD_TIMESTAMP "%Y%m%d.%H%M")
file(READ "${VERSION_H_IN}" VERSION_H_CONTENT)
string(REPLACE "@BUILD_TIMESTAMP@" "${BUILD_TIMESTAMP}" VERSION_H_CONTENT "${VERSION_H_CONTENT}")
file(WRITE "${VERSION_H_OUT}" "${VERSION_H_CONTENT}")
```

This creates a timestamp in format YYYYMMDD.HHMM (e.g., 20251225.1259).

### 2. Template File Contains the Placeholder

`version.h.in`:
```cpp
#define BUILD_TIMESTAMP "@BUILD_TIMESTAMP@"
```

### 3. CMake Custom Target Triggers Generation

`CMakeLists.txt`:
```cmake
add_custom_target(generate_version_h ALL
    COMMAND ${CMAKE_COMMAND}
    -DVERSION_H_IN="${PROJECT_SOURCE_DIR}/version.h.in"
    -DVERSION_H_OUT="${CMAKE_BINARY_DIR}/version.h"
    -P "${PROJECT_SOURCE_DIR}/cmake/GenerateVersionH.cmake"
  COMMENT "Generating version.h with build timestamp"
)
```

### 4. Two About Dialogs Display It

**IMPORTANT:** There are TWO `showAboutDialog()` functions that must stay in sync:

1. `src/app/MainWindow.cpp` - shown from the main application window
2. `src/app/StartupWindow.cpp` - shown from the startup/welcome screen

Both must use the same format:
```cpp
ui.version->setText(QString(tr("version ")) + QString::fromUtf8(VERSION) +
                    QString(" (") + QString::fromUtf8(BUILD_TIMESTAMP) + QString(")"));
```

### 5. UI Label in AboutDialog.ui

`src/app/AboutDialog.ui`: The version QLabel widget receives the text, styled with 10pt bold font, centered horizontally.

### Result

The About dialog displays: `version 2.0a12 (20251227.1153)`

## Adding a New Filter

1. Create directory `src/core/filters/myfilter/`
2. Implement: Filter, Task, CacheDrivenTask, Settings, OptionsWidget, ImageView, Thumbnail
3. Add to StageSequence in `src/core/StageSequence.cpp`
4. Update CMakeLists.txt in filters directory
5. Add UI integration in MainWindow
6. Add `selectionIndicatorLabel` to OptionsWidget.ui for multi-select support

## Common Modifications

### Adjust binarization
`src/imageproc/Binarize.cpp` - Sauvola algorithm parameters

### Change color detection
`src/core/AppleVisionDetector.mm` - Vision framework analysis

### Modify PDF export
`src/core/PdfExporter.cpp` - Quality presets, compression

### Add image filter
`src/imageproc/` - Add new algorithm, integrate in output filter

## Dependencies

- Qt6 (UI framework)
- Boost (C++ utilities)
- libtiff, libpng, libjpeg (image formats)
- libharu (PDF generation)
- leptonica (image processing)
- Metal framework (GPU, macOS only)
- Vision framework (content detection, macOS only)
