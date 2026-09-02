# Architecture of ScanTailor Spectre

A guide for programmers who want to understand, debug, or modify the code.

ScanTailor Spectre is a macOS-only fork of ScanTailor Advanced. It takes raw
scans of book pages and turns them into clean, publication-ready output
(searchable PDFs, TIFFs, PNGs). The goal of this document is to get you to
the point where you can make a change with confidence — not to catalogue
every class.

---

## 1. What kind of program is this?

Spectre is a **pipeline of ten stateless image-processing filters**, each
of which operates on pages independently. The user sees a stage list on the
left, selects a filter, sees the result of that filter on whichever page is
selected, and tweaks parameters until the output looks right. Pressing "play"
on a stage re-runs it across all pages.

The "stateless" part is important: a filter takes an input image + its
parameters + its *dependencies* (a hash of what it depends on upstream) and
produces an output. If nothing upstream changed, the cached result is reused.

This design is what makes Spectre fast on large books, and it's also where
every subtle bug lives, because the definition of "nothing changed" has to
be exactly right.

---

## 2. The ten stages

From `src/core/StageSequence.cpp`, in order:

| # | Directory | What it does |
|---|---|---|
| 1 | `fix_orientation` | 90° rotations. Gets portrait/landscape right. |
| 2 | `page_split`      | Splits a two-page spread into left/right subpages. |
| 3 | `deskew`          | Small-angle rotation to straighten tilted scans. |
| 4 | `page_box`        | Defines the physical page boundary (trim/bleed). |
| 5 | `select_content`  | Defines the content rectangle within the page box. |
| 6 | `page_layout`     | Margins and alignment for the output page. |
| 7 | `finalize`        | Chooses color mode (B&W / Gray / Color). |
| 8 | `output`          | Actual image processing: binarize, despeckle, dewarp. |
| 9 | `ocr`             | Text layer via Apple Vision (optional). |
|10 | `export`          | Writes the final artifact (PDF / images). |

Each stage reads the previous stage's cached result, runs, and writes its
own cache. If you change a parameter in stage 3, stages 4-10 for affected
pages become stale and recompute on demand.

Everything lives in `src/core/filters/<stage>/`. The code you're touching is
almost always inside one of those directories — the shared infrastructure
(`src/core/`, `src/imageproc/`, `src/foundation/`) is surprisingly small.

---

## 3. Anatomy of one filter

Every filter directory has the same seven (ish) classes. Once you understand
one, you understand them all. Using `page_split` as the running example:

```
src/core/filters/page_split/
├── Filter.{cpp,h}            ← the stage's public face
├── Task.{cpp,h}              ← background work that actually runs
├── CacheDrivenTask.{cpp,h}   ← fast path when the cache is warm
├── Settings.{cpp,h}          ← per-page parameter storage (thread-safe)
├── Params.{cpp,h}            ← one page's parameters (value type, XML-serializable)
├── Dependencies.{cpp,h}      ← hash/signature of upstream state
├── OptionsWidget.{cpp,h,ui}  ← Qt options panel
├── ImageView.{cpp,h}         ← widget that shows the stage's output
└── Thumbnail.{cpp,h}         ← small preview used in the page list
```

### Filter

Implements `AbstractFilter` (`src/core/AbstractFilter.h`). Its job is
plumbing:
- hand out a singleton `Settings` instance,
- build a `Task` when asked to process a page,
- save/load its own XML section of the project file,
- provide an `OptionsWidget` for the UI.

Never put real work here. It's a factory and a thin UI adapter.

### Task and CacheDrivenTask

`Task` does the real background work for one page. It receives a
`FilterData` (input image + transform) from the previous stage's Task, runs
the algorithm, stores the result in `Settings`, and produces a `FilterData`
for the next stage. Tasks are created and chained in
`src/core/ProjectPages.cpp` and run in a thread pool
(`src/core/BackgroundExecutor.cpp`).

`CacheDrivenTask` is the fast path: when cached params already exist and
dependencies are compatible, no computation runs — only downstream tasks
get triggered. Both Task variants exist so that re-running a later stage
doesn't drag the whole project through actual image decoding again.

For `page_split`, the task eventually runs a layered detector:

- Apple Vision estimates whether the scan is a two-page spread from text
  regions and page-number candidates. Its result is useful as an anchor,
  but it should not be treated as exact geometry on photo-heavy books.
- `SpineDarknessFinder` refines that anchor by scanning candidate vertical
  gutter columns. It favors columns that are dark across much of the page
  height, locally prominent, and consistent with a visible binding shadow.
- Narrow rescue paths handle cases that the normal gates intentionally
  reject, such as a dark gutter beside a photograph or a dark gutter between
  photo material and text. Keep these rescues specific and documented. Broad
  "center wins" or "darkest edge wins" rules have repeatedly regressed other
  spreads.
- Light/no-gutter spreads are protected by stricter rescue gates, including
  minimum darkness/continuity checks and rejection of isolated off-anchor dark
  strokes.

Version 2.0a24 predated most of this layered spine refinement. The current
2.0a25 line uses detector-versioned page-split params so cached results are
invalidated whenever the page-split algorithm changes.

### Settings, Params, Dependencies

- **Settings** is a per-filter, thread-safe container of `(PageId → Params)`.
  Held by the Filter as a `std::shared_ptr`.
- **Params** is a value type. Immutable once constructed. Serializes to a
  `<params>` XML element. A `Params` instance always carries a
  `Dependencies` next to it so the loader knows what upstream state
  produced it.
- **Dependencies** captures *everything that, if changed upstream, should
  invalidate this stage's cached result*. Typically: image size, rotation,
  layout mode, and — critically — an integer `detectorVersion` that you
  bump when the algorithm itself changes. See the "cache invalidation"
  section for why this matters.

### OptionsWidget / ImageView / Thumbnail

Pure Qt. The UI reads from / writes to `Settings`. `ImageView` inherits
from `ImageViewBase` (`src/core/interaction/ImageViewBase.cpp`), which
handles zoom/pan/transform and dispatches mouse events through
`InteractionHandler` subclasses.

---

## 4. Data flow through the pipeline

```
(raw image from PdfReader / disk)
      │
      ▼
┌────────────────┐       ┌───────────────┐
│ Task (stage N) │ ────▶ │ FilterData    │ ────▶ Task (stage N+1)
└────────────────┘       │ (image +      │
      │                  │  ImageTransform│
      ▼                  │  + metadata)  │
┌────────────────┐       └───────────────┘
│ Settings       │            │
│ (per-page      │            ▼
│  Params)       │       (feeds next stage's
└────────────────┘        Task or CacheDrivenTask)
```

`FilterData.grayImage()`, `FilterData.origImage()`, and `FilterData.xform()`
are the three things each Task normally reads. The `xform` is an
`ImageTransformation` — a `QTransform` plus metadata — that records the
accumulated rotations/crops from all upstream stages. This is how stage 5
can draw a rectangle "on the original image" even though stage 2 split the
spread and stage 3 rotated it.

**Pages run in parallel.** Each page's Task can run on its own thread. Do
not assume any ordering of `qDebug` output. When the page-split detector was
being debugged tonight, log lines for different PDF pages interleaved
badly, which is why Task.cpp tags its diagnostics with `pdfPage=N` so you
can disentangle them.

---

## 5. Coordinate systems (the part that will bite you)

There are **three** coordinate systems, and `ImageViewBase` translates
between them:

1. **Image coords** — integer pixel positions in the original bitmap
   (as it was stored on disk or rendered from the PDF).
2. **Virtual coords** — floating-point positions in the page's logical
   output space. This is the image after all the upstream transforms
   (rotation, page-split crop, deskew) have been applied. Measurements in
   millimetres or DPI are derived from here.
3. **Widget coords** — screen pixels in the Qt widget, including zoom/pan.

Every `ImageView` holds:
- `m_imageToVirtual` — a `QTransform` mapping image → virtual
- `m_virtualToWidget` — the current zoom/pan/centering

The three common bugs are all confusions between systems:
- Drawing an overlay in virtual coords when the hit-test is in widget
  coords (off-by-a-zoom-factor).
- Storing mouse positions in widget coords and forgetting to unproject
  them before comparing across frames.
- Reading raw image pixels at a virtual-coord x when the transform
  includes a rotation (use `map()`).

When in doubt, read `ImageViewBase::imageToVirtual()` and
`ImageViewBase::virtualToWidget()` and copy how a working filter does it.

---

## 6. Project files, caches, and cache invalidation

A project file is a `.ScanTailor` XML document. It records:
- the list of source files (PDFs / images),
- per-image/per-subpage `<image>` nodes with the cut layouts,
- per-filter `<filter>` nodes with `<page id="N"><params>…</params></page>`
  children.

The `ProjectReader` and `ProjectWriter` classes (`src/core/`) walk this
tree. Each filter's `Filter::saveSettings()` and `loadSettings()` serialize
*their* subtree and *their* `Settings` container.

### Dependencies and compatibleWith

This is the most important thing to understand if you modify a filter's
algorithm. When a `Task` runs, it builds a fresh `Dependencies` from the
current upstream state, then compares it to the `Dependencies` that was
stored alongside the cached `Params`:

```cpp
// src/core/filters/page_split/Dependencies.cpp (simplified)
bool Dependencies::compatibleWith(const Params& params) const {
  const Dependencies& deps = params.dependencies();
  if (splitLineMode() == MODE_AUTO
      && m_detectorVersion != deps.m_detectorVersion) {
    return false;            // algorithm changed — recompute
  }
  if (m_imageSize != deps.m_imageSize) return false;
  if (m_rotation != deps.m_rotation) return false;
  if (!layoutTypesCompatible(m_layoutType, deps.m_layoutType)) return false;
  return true;
}
```

**If you change the algorithm, bump `kPageSplitDetectorVersion` (or the
filter's equivalent).** Otherwise cached results from the old algorithm
will be considered valid and the new code will never run on pages the
user already processed. This has burned me twice in one night.

### The cache directory

Big intermediate artifacts (binarized pages, dewarp maps, OCR hOCR files)
live in `<project>/cache/…`, indexed by page id. If a cache file exists
and dependencies match, the task skips work and returns a reference to
the file.

---

## 7. Concurrency

`BackgroundExecutor` is a fixed-size thread pool (`src/core/`). Each
page's task graph is submitted to it and runs to completion. The UI thread
waits for results via Qt signals.

Rules:
- `Settings` access *must* go through its mutex. Every filter's
  `Settings::setPageParams` etc. takes a `QMutexLocker`.
- Never call Qt widget methods from a worker thread — emit signals and let
  the UI slot handle them.
- Never assume the order pages finish in. Use the page id carried through
  `FilterData` to disambiguate.

If you add a new shared structure, assume it will be read from any thread
at any time, and guard it accordingly.

---

## 8. Apple-specific bits

Spectre depends on macOS for more than just the UI framework.

- **CoreGraphics PDF**: `src/core/PdfReader.mm` (read) and
  `src/core/PdfExporter.mm` (write) use `CGPDFDocument` and
  `CGPDFContext`. This is why the app is macOS-only; the legacy Qt5 fork
  used `Qt::Pdf` which requires macOS 12+. CG lets us support macOS 11.
  Orientation/matrix math in PDFs is a minefield; see the top of
  `BUILD_LOG.md` for a long history of failed attempts.
- **Apple Vision**: `src/core/AppleVisionDetector.mm` wraps the Vision
  framework's text-recognition APIs. Used for (a) the page-split detector's
  coarse "where is the text gutter?" pass, and (b) OCR in stage 9. The
  Vision framework is much faster than Tesseract but it's a black box —
  you get text regions and confidence, not glyph-level detail.
- **Metal**: `src/acceleration/Metal*` was a GPU path for Gaussian blur
  and morphology. It's currently **disabled** because (1) backgrounding
  the app purged GPU resources and crashed the next blur, and (2) the
  morphology dilation shader had an off-by-one that produced `dilated >
  original` and tripped an assertion in `mokjiThreshold`. See
  `TODO.md` for the re-enablement plan. Do not depend on it running.

---

## 9. Build, sign, release

The canonical how-to is in `CLAUDE.md` under "Build Commands" — I'm not
going to duplicate it here because it drifts. The short version:

```bash
# Dependencies (once)
brew install qt6 boost libtiff libpng jpeg cmake leptonica

# Build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) ..
cmake --build . -j$(sysctl -n hw.ncpu)
```

**Before every build**, append an entry to `BUILD_LOG.md` describing what
you're changing. This is a hard project rule. Releases are tagged
`v2.0aNN`, signed with Developer ID, notarized via `xcrun notarytool`,
and shipped as timestamped DMGs. The full sequence is in `CLAUDE.md`.

---

## 10. Tips for making a change

Based on actually debugging this code:

### Start from the pipeline, not the widget
When a page looks wrong, the question is almost never "is the widget
painting correctly?" — it's "which stage produced the wrong `Params`?".
Work backwards from the visible symptom through the stages until you
find the one where the output stops matching your expectation.

### Use `qDebug()` liberally, log the page id
Because tasks run in parallel, **always tag your log output with enough
context to know which page it belongs to**. Look at the diagnostics in
`page_split/Task.cpp` for the pattern: `"pdfPage=" << m_pageInfo.imageId().page()`.
Without this tag you will chase ghosts across interleaved log lines.

### Render the same view offline when you're stuck
When the page-split detector was misbehaving, rendering the relevant PDF
page with `pdftoppm -r 200 -f N -l N` and analyzing the brightness
profile in Python (`PIL` + `numpy`) was faster than reading the detector
log. The offline analysis has to match what the in-process downscaled
image looks like — different DPI ratios can hide bugs.

### If the detector code changed, bump the version
See §6. This is the number one cause of "I fixed it but it doesn't
work" — your fix is correct, but the cache is serving the old result.

### Prefer one more `if` over a new gate
The page-split detector uses a small set of gates (`kMinMeanDarkness`,
`kMinDarkRowFraction`, etc.) to classify candidate columns. Adding a
new gate globally is risky — it may regress pages you didn't test.
Prefer adding a narrow acceptance path inside the existing gate
function, guarded by specific conditions, with a comment explaining
the exact pattern it addresses. See the "binding fold against text"
path in `SpineDarknessFinder::findSpine` for an example.

### When confused, dump the decision
Whenever a filter picks between multiple candidates (splits, peaks,
lines), log all the candidates it considered and mark the winner.
`SpineDarknessFinder` does this with its `top N spatially-separated
candidates` dump. Adding a dump like this takes ten minutes and saves
hours of guessing.

### The project XML is your friend
If something is mysteriously wrong after a user action, open the
`.ScanTailor` file in a text editor. Every filter's stored state is
there, keyed by page id. You can compare before/after and see exactly
what changed.

---

## 11. Places you'll probably never need to touch

So you don't waste time browsing:

- `src/dewarping/` — the cylindrical-surface unwrap math. Solid and
  low-traffic. Avoid unless you're chasing a dewarp bug specifically.
- `src/math/` — splines, matrices. Used by dewarping, basically static.
- `src/foundation/` — tiny utilities (`IntrusivePtr`, `XmlMarshaller`).
  Boring, and you should not rewrite them.
- `src/acceleration/` — Metal shaders. Currently dead code; see §8.

---

## 12. Where the bodies are buried

Honest footnotes about things that surprised me:

- **Auto page-split on asymmetric spreads is the hard case.** When one
  page is a photo and the other is text, there's no symmetric signal
  for the detector. The codebase now has a dark-spine detector, a
  paper-gap detector, and a Vision-based detector, and they cooperate.
  None of them are complete. `SpineDarknessFinder.cpp` is the most
  heavily iterated file in this fork.
- **Image dimensions can change between a task's recomputes** in ways I
  don't yet fully understand. The same PDF page can be analyzed with
  two different aspect ratios in the same session, producing different
  results. If you see a cache-invalidation that isn't explained by a
  detector-version bump or a user action, this may be the cause.
- **The "Reset all auto pages" button triggers a lot of recomputes in
  parallel.** Log interleaving gets chaotic. Work with one page at a
  time when debugging.
- **Qt's QLineF, QPointF, and QRectF are cheap to construct** but
  `QLineF::isNull()` returns true only for default-constructed lines,
  not for lines where p1 == p2. Watch for this.
- **`data.origImage()` is not always the raw scan.** It's whatever the
  previous stage's Task passed in `FilterData`, which may already have
  rotation or cropping baked in through `FilterData::xform()`. Read
  the transform, don't assume image coords equal source coords.

---

## 13. Reading order for a new contributor

If you have an afternoon:

1. `src/core/AbstractFilter.h` — the filter interface (50 lines).
2. `src/core/StageSequence.cpp` — where the ten filters are wired together.
3. `src/core/filters/page_split/Filter.cpp` — a representative filter.
4. `src/core/filters/page_split/Task.cpp` — its background task.
5. `src/core/filters/page_split/Dependencies.cpp` — cache-invalidation logic.
6. `src/core/BackgroundExecutor.{cpp,h}` — the thread pool.
7. `src/core/interaction/ImageViewBase.cpp` — the base view class.

After that, dive into whichever filter you're touching. The patterns
repeat.

---

Welcome aboard. Log to `BUILD_LOG.md` before you build, and don't forget
to bump the detector version.
