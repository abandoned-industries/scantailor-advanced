# ScanTailor Spectre — Architecture Audit

**Audit date:** 2026-08-26
**Commit audited:** `a9984ca1` (main; child of `ca6c4b49` differing only in `dist/`/`tmp/` release artifacts — the `src/` tree is identical under both, and individual auditors cite either hash)
**Scope and method:** read-only, five-dimension parallel audit (layers/dependencies, god objects, state/persistence, concurrency, build/naming/docs) of `src/` — 130,305 lines across 590 `.cpp/.h/.mm` files (94,040 lines in the 458 implementation files), excluding `build/`, `dist/`, `tmp/`, `.claude/worktrees/`. Include graph built by scripted scan of every `#include`/`#import`; line ranges are grep-derived and accurate to ±a few lines. No code was changed, and this report proposes no fixes, stages, or extractions — it is an inventory of what is, with the questions that must be answered before anyone acts. A staged proposal is a separate, later task.
**Name strata:** the repo is `scantailor-weasel` (legacy codename), the shipping product is **ScanTailor Spectre**, the upstream is ScanTailor Advanced. This report uses the shipping name, Spectre, throughout; "weasel" appears only when naming the actual `src/core/weasel/` directory and its namespace.

---

## One-page summary

Ranked by leverage-to-risk: what buys the most maintainability for the least chance of changing behavior. Everything below is expanded, with evidence, in sections 1–7.

### Outright defects (behavior is wrong or dangerous today)

1. **DeviationProvider data race** — a genuine main-vs-worker race exercised on every batch run: four filters' `Settings` embed a lazily-rebuilt `DeviationProvider` whose `const` getters rewrite an internal map with no lock, read from the GUI thread on every thumbnail repaint while workers insert under a mutex the reader never takes (`src/core/DeviationProvider.h:43-115`; ~8 racing read sites across deskew, select_content, page_box, page_layout). Worst case is heap corruption in a Settings object shared by every page. Highest-leverage defect in the codebase. (§5)
2. **PDF import render DPI is not serialized** — `PdfReader`'s static `s_importDpiMap` (`PdfReader.mm:556-573`) and `output::Settings::m_defaultDpi` live only in memory; a project imported at 600 DPI reopens rendering at 300 while the XML still carries 600-DPI metadata. (§3, F1)
3. **TIFF compression UI is a no-op** — finalize's compression choice is never synced to the `ApplicationSettings` keys `TiffWriter` actually reads; the comment at `output/Task.cpp:64-66` claiming otherwise is false, and the enum values wouldn't even agree. Format/quality also reset on every reload (none of finalize's output block is serialized). (§3, F2)
4. **`$TMPDIR` output dir can be serialized into the project file** — unsaved/PDF-import projects write outputs to `$TMPDIR/scantailor-spectre-<md5(path)[:8]>`; saving such a project stores that path (often *relative*, through `/var/folders/…`) in the XML, and nothing recreates it on reopen. Two projects from the same source path also share one temp dir, and closing one recursively deletes it. (§3, F6; §6)
5. **Project XML round-trip drops unknown content behind a hard version wall** — `ProjectWriter` rebuilds the document from memory (nothing unknown survives a save), `ProjectReader` requires `version == 4` exactly in both directions, and defaults profiles share the same version constant. Malformed per-page entries silently revert to defaults, then the next save permanently erases them. (§6)
6. **Cancel lands after the decode** — `LoadFileTask::operator()` calls `ImageLoader::load` *before* its first `throwIfCancelled()` (`LoadFileTask.cpp:61-64`): a cancelled 600-DPI PDF page still pays a full CoreGraphics rasterization, and `~MainWindow` blocks the GUI thread waiting for it. (§5)
7. Smaller defects: worker-thread `QPixmap::fromImage` (platform-contract violation, `ThumbnailPixmapCache.cpp:545`); output staleness matches files by **size only** while OCR checks size+mtime — two notions of "same file" in one pipeline; `settings/batch_processing_threads` key duplicated as a literal in two files; per-page white-balance overrides in output don't survive reload (F4); `external_alarm_cmd` runs a hand-editable INI value through `std::system()`.

### Structural debt (works, but resists change)

8. **MainWindow.cpp — 5,133 lines, 198 methods, ~55 members, 125 connects** — the one true god object: batch + auto-mode state machine, PDF import/export, four summary dialogs with remediation policy, task factory hard-wiring all 10 filters, plus 50 direct filter-internal includes making it the choke point for any filter API change. (§4)
9. **core ↔ filters strongly connected component** — core includes its own plugins (`StageSequence.h`, `DefaultParams.h`, three propagators) while ~520 filter→core includes come back, and core↔interaction↔zones adds a declared target cycle: **13 directories, ~610 files, ~70K lines that cannot be built, tested, or extracted independently**. The per-filter CMake targets are compile-parallelism units, not boundaries. (§2)
10. **OutputGenerator::Processor** — a file-local nested class of 35 members and 34 methods; its two ~300-line render drivers (`processWithoutDewarping` / `processWithDewarping`) are near-parallel copies sharing the same member soup. (§4)
11. Secondary: `output::Task::process` is one 478-line method; `DefaultParamsDialog` hand-mirrors six filters' options UIs (every new output option = ≥3 edit sites); `output/Settings.h` and `ColorParams.h` are de-facto shared model types (26/19 includers) living inside a filter; core PUBLIC-links QtWebEngine (~200 MB in every bundle) for one options panel.

### Dead weight (delete-or-decide; near-zero behavioral risk)

12. **libharu** — found, linked, documented, and referenced by zero source lines. **ImageTypeDetector** — compiled into core, zero references, superseded by LeptonicaDetector. **Metal morphology** — hard-disabled (`MetalMorphology.mm:25-29` returns false unconditionally) yet fully built and shader-bundled; docs say all Metal is disabled, but Gauss blur is live. **Tracked `tmp/` release bundle** — 707 tracked files (~622 MB dir), apparently accidental. **Stale docs** — `packaging/macos/BUILD.md` wrong end to end, `compatibility.md` superseded, 5 root session artifacts, plus enumerated CLAUDE.md errors ("8 filters", "macOS 11+", wrong detector pointer, missing `PKG_CONFIG_PATH`). Also: ~200 lines of unreachable WIN32/Linux CMake, `fix_orientation.html` shipped with no code path, dead `cache/predespeckle/`, dead `preserveOutput` state, 5 unregistered diagnostic executables built on every build. (§§1, 7, 8)

### Healthy (verified, leave alone)

The imageproc algorithm libraries (Binarize, Morphology, BinaryImage et al.) are cohesive, not god objects. The 10-stage filter template is uniform and navigable. The threading architecture is disciplined — all 10 filter Settings mutexed, cross-thread delivery via postEvent, no DirectConnection, no detached threads. Portable relative project paths are cleanly owned and round-trip tested. ThumbnailSequence is well-factored. ARCHITECTURE.md, SIGNING.md, and docs/zotero-loop.md are accurate. The Qt side is clean Qt6 with zero deprecated-API hits.

---

## 1. Layer map

CMake declares a strict ladder — `foundation ← math ← imageproc ← dewarping ← core ← interaction ← zones`, filters on top of core, `app` linking only core + EXTRA_LIBS (`src/app/CMakeLists.txt:92-94`). The include graph honors the bottom half of that ladder and thoroughly violates the top half (§2).

Line counts per layer (src/ total 130,305): foundation 3,118; math 4,555; imageproc 23,361; dewarping 5,816; acceleration 850; core 77,069; app 15,536.

### foundation — 44 files, 3,118 lines

Intended dependency-free utilities; actually utilities plus small math value types (`VecNT.h`, `MatMNT.h`, `Proximity.h`), XML marshalling on QtXml, and Qt-dependent helpers. Links Qt Core/Xml/Gui PUBLIC (`src/foundation/CMakeLists.txt:35`), so nothing above foundation is Qt-free. Zero includes of any other project layer. One straddler by semantics, not includes: `foundation/BatchProcessingContext.h` defines a process-global `std::atomic<int> activeTasks` (line 11) coupling `core/WorkerThreadPool.cpp`, `core/ThumbnailPixmapCache.cpp`, and `imageproc/Binarize.cpp:13` through shared mutable global state — an app-pipeline concept placed in foundation so imageproc can see it without including core. Layering preserved syntactically, bypassed via a global side channel.

### math — 42 files (+4 tests), 4,555 lines

Splines (`XSpline.cpp` 801 lines), spline fitting, auto-diff, projective geometry. Depends only on foundation (38 includes). Clean.

### imageproc — 102 files (+21 tests), 23,361 lines

Pixel-level algorithms (`BinaryImage`, `Binarize.cpp` 2,117 lines, `Morphology.cpp` 1,267 lines). Depends on foundation (27), math (2), and **acceleration** (2: `GaussBlur.cpp:15`, `Morphology.cpp:17`, both `#ifdef Q_OS_MACOS`). Uses QtGui heavily (24 headers reference `QImage`) but zero QtWidgets — the single grep hit (`DebugImages.h:14`) is a forward declaration used in a `std::function<QWidget*(const QImage&)>` factory parameter, a deliberate dependency-inversion hook. One Apple-framework leak: `Morphology.cpp:18` includes `<Accelerate/Accelerate.h>` directly, `Q_OS_MACOS`-guarded.

### dewarping — 22 files, 5,816 lines

Page-flattening models. Depends on imageproc (28), foundation (27), math (20). Clean.

### acceleration — 8 files, 850 lines, all Objective-C++/Metal

`MetalContext`, `MetalGaussBlur`, `MetalMorphology`, `MetalLifecycle` + `.metal` shaders compiled to `default.metallib`. Positioned *below* imageproc in CMake (imageproc links it). Runtime status is split, contradicting the project CLAUDE.md claim that Metal is "currently disabled":

- Metal **morphology is hard-disabled**: `metalMorphologyAvailable()` returns `false` unconditionally with a crash TODO ("crash in objc_msgSend during dispatch_sync"; `src/acceleration/MetalMorphology.mm:25-29`). The GPU morphology path, including its `dispatch_sync`, is compiled-but-unreachable dead code; the whole static lib, shaders, and metallib bundling remain fully built.
- Metal **Gaussian blur is live**: `metalGaussBlurAvailable()` returns `isAvailable() && metalIsAppActive()` (`MetalGaussBlur.mm:57-61`) — foreground-gated via `MetalLifecycle.mm` NSNotification observers — and is called from `imageproc/GaussBlur.cpp:91,103`. `TODO.md`'s "Current Task: re-enable Metal" is partially stale: its Phase 1 (lifecycle observer) is already implemented.

`app/main.cpp:23` also includes `MetalLifecycle.h` directly (app→acceleration, bypassing the ladder; resolves only through global include dirs and static-link transitivity).

### core — 243 files, 77,069 lines: the grab-bag

Four distinct kinds of code in one directory/one static lib:

1. **Domain model / project state**: `PageId`, `ImageId`, `ProjectPages`, `PageSequence`, `ProjectReader/Writer`, `DefaultParams.h` (458 lines).
2. **Pipeline machinery**: `AbstractFilter.h`, `StageSequence`, `BackgroundExecutor`, `LoadFileTask`, `FilterResult`.
3. **UI widgets**: 24 files include widget-class headers; 14 headers define widgets (`ImageViewBase.h:47` is a `QAbstractScrollArea`; `StageListView`, `ErrorWidget`, `FilterOptionsWidget`, `SkinnedButton`, `CollapsibleGroupBox`, `ThumbnailBase`, …).
4. **macOS I/O bridges (Objective-C++)**: `PdfReader.mm`, `PdfExporter.mm`, `PdfReadError.mm` (CoreGraphics/CoreText), `AppleVisionDetector.mm` (Vision) — the only four `.mm` files in core. Apple frameworks are linked PRIVATE to core (`src/core/CMakeLists.txt:203-208`) and framework types do not leak into any core header.

Also `ZoteroLoopSidecar.{h,cpp}` (160 lines), shared by app and the export filter. core links **Qt::WebEngineWidgets and Qt::WebChannel PUBLIC** (`src/core/CMakeLists.txt:210`) solely to serve `core/weasel/` — the single reason the ~200 MB QtWebEngineCore framework ships in every bundle.

### core/interaction (21 files) and core/zones (30 files)

Mouse-interaction framework and zone editing; separate CMake targets that link *upward* (`interaction PUBLIC core`, `zones PUBLIC interaction`) while core links `zones` PUBLIC — a declared three-way target cycle core → zones → interaction → core, harmless on a static-lib link line, meaningless as a boundary. Include reality matches: interaction→core 3, core→interaction 4, zones→core 5, core→zones 3.

### core/filters/* — 10 stage directories, 316 files

Uniform template: `Filter`, `Task`, `CacheDrivenTask`, `Settings`, `OptionsWidget` each exist in 10 directories, `ImageView.h` in 9 — the identical basenames are real collisions resolved by directory. Sizes are wildly uneven: **output is 107 files, a third of all filter code** (`OutputGenerator.cpp` alone 3,309 lines, `OptionsWidget.cpp` 1,795); next largest page_split 39 files; smallest export 12. Each filter depends on core (30–101 includes each), is depended on *by* core (§2.2), and filters include each other in a chain (§2.3).

### core/weasel — 10 files, 933 lines. Live, and the fork's namesake.

Two unrelated things share the directory:

- **Data/algorithm**: `PhotoAdjustments.h` (pure data + QDom serialization) and `TonalCurve.{h,cpp}` (327 lines, pure LUT math on QImage). Consumed by the output pipeline itself: `filters/output/ColorParams.h:10` embeds PhotoAdjustments in the serialized per-page params; `OutputGenerator.cpp:60` and `output/Task.cpp:46` apply TonalCurve during rendering.
- **QtWebEngine UI**: `WebOptionsPanelBase` (QWebEngineView + QWebChannel host loading HTML from `qrc:/weasel/webui/`), `GenericPanelBridge`, `PhotoAdjustmentsWebView` — consumed only by `filters/output/OptionsWidget.cpp:18-21`.

Weasel has zero outgoing project includes, is covered by `core/tests/TestPhotoAdjustments.cpp`, and is definitively live. Dead spot: `webui/webui.qrc` ships `fix_orientation.html`, but the only `WebOptionsPanelBase` instantiation loads `photo_adjustments.html` (`PhotoAdjustmentsWebView.cpp:55`); no `.cpp/.h/.js` references `fix_orientation.html` — an apparently abandoned or not-yet-started second web panel. The directory is also misnamed by function (naming finding, §7): nothing about "weasel" says "web-based options panels".

### app — 57 files, 15,536 lines

The executable: `MainWindow.cpp` (5,133 lines — largest file in the repo, implementing `FilterUiInterface` privately, `MainWindow.h:71`), `ThumbnailSequence.cpp` (1,669), `DefaultParamsDialog.cpp` (1,124), ~15 dialogs, `StartupWindow`, `AppController`, `ZoteroPluginInstaller`, `AccessibilitySafeguard`. Correctly at the top: nothing anywhere includes app headers. But app reaches *around* core into filter internals: 54 includes of `core/filters/*` headers, 50 of them from `MainWindow.cpp:108-157` (every filter's Task/CacheDrivenTask/Settings, several Params/Filter/OptionsWidget) plus `DefaultParamsDialog.cpp:8-11`. `StageSequence.h` is included by only 4 files total — it is not the encapsulation boundary its name suggests; MainWindow constructs the pipeline by hand. `src/app/CMakeLists.txt:98` injects `src/dewarping` as an include dir only because `filters/output/Params.h:7` includes `<dewarping/DistortionModel.h>` — a transitive-header patch, not a real app→dewarping dependency (0 direct includes).

### Other trees

- `src/resources/`, `translations/`: assets and .ts files, no code.
- Tests: `math/tests` (4), `imageproc/tests` (21), `core/tests` (26 — a Boost.Test `core_tests` binary plus 8 additional executables including 5 standalone diagnostic CLI tools, all built unconditionally). **No `app/` tests exist.**
- **zotero-plugin/**: a JavaScript Zotero extension (bootstrap.js + XUL dialog, built to `st-spectre-loop.xpi` by `zotero-plugin/build.sh`). It never touches the C++ compile; contact points are the `scantailor_bundle` target (copies and version-validates the prebuilt `.xpi` into Resources) and the C++ counterparts `core/ZoteroLoopSidecar`, `core/ZoteroClient`, `app/ZoteroPluginInstaller`. Live, not duplicated; its JS tests (`zotero-plugin/test/{dialog,layout,smoke}.js`) are not wired into CTest.

### Non-findings

- No upward includes into app from any layer (grep across foundation, math, imageproc, dewarping, acceleration, core: zero project-header hits).
- No QtWidgets in foundation, math, imageproc, dewarping, or acceleration (sole hit is the forward-declaration hook in `DebugImages.h:14`).
- No core→app, imageproc→core, dewarping→core, or math→imageproc includes (0 edges each after basename-collision-aware resolution).
- `core/weasel` is not dead — consumed at render time, settings time, UI, and tests.
- `zotero-plugin/` does not enter the C++ build.
- No abandoned parallel rewrite is compiled into the app — the weasel web panel coexists with the classic Qt widgets in `output/OptionsWidget.ui` by design; both live.

### Unverified

- The dependency script's basename-collision resolution mirrors, but was not validated against, the compiler's actual `-I` search order for the ~15 colliding names (`Filter.h` ×10, `Task.h` ×10, `Settings.h` ×10, `Utils.h` ×9, …); per-file counts could shift by a few either way.
- `fix_orientation.html` dead-resource claim rests on grep over `.cpp/.h/.js`; a runtime-constructed string would evade it (none found, but not provable).
- Runtime reachability of the live Metal Gauss-blur path on current hardware/OS (code path verified live; `isAvailable()` not executed). Likely returns false inside test binaries (no bundle → `newDefaultLibrary` nil → CPU-only in tests) — inferred, not executed.

---

## 2. Dependency direction

### 2.1 Aggregate picture (include counts between layer directories)

Downward, matching CMake, healthy: math→foundation 38; imageproc→foundation 27, →math 2; dewarping→imageproc 28, →foundation 27, →math 20; every filter→core (30–101 each), →imageproc, →foundation; app→core 126, →foundation 12, →imageproc 3.

### 2.2 core ↔ filters — the load-bearing inversion

core (the base library) includes its own plugins:

- `core/StageSequence.h:12-21` includes all ten `filters/*/Filter.h`.
- `core/DefaultParams.h:6-14` includes six output/page_layout/page_split option headers (`filters/output/ColorParams.h`, `DepthPerception.h`, `DespeckleLevel.h`, `DewarpingOptions.h`, `PictureShapeOptions.h`, `filters/page_layout/Alignment.h`, `filters/page_split/LayoutType.h`).
- `core/LoadFileTask.cpp:23` → `filters/fix_orientation/Task.h`; `core/ContentBoxPropagator.cpp:13` → `filters/page_layout/Filter.h`; `core/PageOrientationPropagator.cpp:13` → `filters/page_split/Filter.h`.

20 core→filters includes total, against ~520 filters→core. Combined with core↔interaction↔zones (§1), this makes **one strongly connected component of 13 directories (core, interaction, zones, all 10 filters) ≈ 610 files / ~70K lines**: no filter, nor core, nor interaction/zones can be built, tested, or extracted independently. `AbstractFilter` exists but core does not stay on the abstract side of it.

### 2.3 Filter→filter chain and mutual pairs

Each stage includes a neighbor's Task/CacheDrivenTask (the "construct the next task" pattern): fix_orientation→page_split (4 includes), page_split→deskew (4), deskew→page_box (4), page_box→select_content (4), select_content→page_layout (4), page_layout→finalize (4), finalize→output (6). At the tail the direction flips: ocr→output (6), export→ocr (7), export→output (3). Mutual (cyclic) pairs:

- **finalize ↔ output**: 6 / 2 (`output/ApplyColorsDialog.h:16`, `output/OptionsWidget.cpp:17` → `filters/finalize/Settings.h`).
- **page_box ↔ select_content**: 4 / 2 (`select_content/Filter.cpp:24`, `select_content/Task.cpp:21` → `filters/page_box/Settings.h`).
- **page_layout ↔ finalize**: 4 / 1 (`finalize/OptionsWidget.cpp:12` → `filters/page_layout/ApplyDialog.h`).

### 2.4 Other direction anomalies

- imageproc → acceleration: downward per CMake, but architecturally "algorithm depends on GPU backend" (`GaussBlur.cpp:15`, `Morphology.cpp:17`).
- app → acceleration directly (`main.cpp:23`), not via any linked dependency of the app target.
- app → filter internals: 54 includes (§1).
- Global-state coupling through `foundation/BatchProcessingContext.h` (§1).

### 2.5 Hub headers (include occurrences across src, script-counted)

| # | Header | Layer | Count |
|---|--------|-------|-------|
| 1 | foundation/NonCopyable.h | foundation | 91 |
| 2 | core/PageId.h | core | 71 |
| 3 | imageproc/BinaryImage.h | imageproc | 65 |
| 4 | imageproc/Dpi.h | imageproc | 27 |
| 5 | core/FilterResult.h | core | 27 |
| 6 | foundation/TaskStatus.h | foundation | 27 |
| 7 | **core/filters/output/Settings.h** | **filter** | **26** |
| 8 | core/ImageTransformation.h | core | 26 |
| 9 | core/ImageViewBase.h | core | 26 |
| 10 | imageproc/Grayscale.h | imageproc | 26 |
| 11 | imageproc/GrayImage.h | imageproc | 25 |
| 12 | core/ImageId.h | core | 24 |
| 13 | core/PageSequence.h | core | 24 |
| 14 | core/PageSelectionAccessor.h | core | 23 |
| 15 | core/ProjectPages.h | core | 23 |

(Next: FilterUiInterface.h 22, FilterData.h 22, ApplicationSettings.h 21, Dpm.h 21, IconProvider.h 20, RasterOp.h 20, VecNT.h 20, **filters/output/ColorParams.h 19**, RelinkablePath.h 19, AbstractRelinker.h 18.)

Two *filter-private* headers — `output/Settings.h` at 26 and `output/ColorParams.h` at 19 — rank among the top hubs: the output filter's settings are de-facto shared model types used by finalize, ocr, export, core (`DefaultParams.h`), app, and tests.

### 2.6 CMake target structure vs include reality

- **Declared cycles**: core PUBLIC-links all ten filter libs (`src/core/CMakeLists.txt:212-214`) while every filter PUBLIC-links core; core→zones→interaction→core; and `export_ PRIVATE ocr` + `ocr PRIVATE export_` — the latter half spurious: ocr includes **zero** export headers (verified by grep), so `ocr → export_` is a declared-but-unused link.
- **Used-but-undeclared** filter→filter deps (target links only core): deskew→page_box, fix_orientation→page_split, page_split→deskew, page_box→select_content, select_content→page_layout, page_layout→finalize, output→finalize. Only export_, ocr, finalize declare inter-filter links. Everything compiles because every include resolves through core's PUBLIC include dir and symbols resolve on the final static-link line — **CMake would not catch a new cross-filter include anywhere**.
- app's extra `src/dewarping` include dir papers over `output/Params.h`'s transitive dewarping include rather than reflecting a real dependency.

### 2.7 Apple framework distribution

Confined by scan: acceleration 5 files (Metal, MetalPerformanceShaders, QuartzCore); core exactly the 4 `.mm` bridges; imageproc 1 file (`Morphology.cpp` → Accelerate, guarded); core/tests 1 (`TestPdfExporter.cpp`). No Apple types in any project header outside acceleration (`MetalContext.h` is the only header importing a framework).

### Non-findings

Covered with §1's (same auditor): no upward includes into app; the CMake ladder's bottom half (foundation→math→imageproc→dewarping) is honored exactly.

### Unverified

- Qt signal/slot and `QMetaObject`-based coupling was not traced; string-based connections could hide additional app↔filter coupling invisible to the include graph.
- Whether `ocr ↔ export_`'s declared link cycle ever mattered at link time (past symbol use) — only current includes were checked.

---

## 3. State ownership

### 3.1 The owners

| State | Single source of truth | Where | Serialized to | Notes |
|---|---|---|---|---|
| Page/image collection, sub-page layout, removed halves | `ProjectPages` | `src/core/ProjectPages.h:32` | project XML `<images>`/`<pages>` | Mutex-guarded; emits `modified()` |
| Per-filter per-page params (×10) | each `filters/<stage>/Settings` | e.g. `filters/output/Settings.h:33` | per-filter `<filters>/<stage>` element | All 10 are `QMutex`-guarded `unordered_map<PageId, …>` (verified in all 10 Settings.h) |
| Selected page | `SelectedPage m_selectedPage` (MainWindow) | `MainWindow.cpp` | `selected` attr (`ProjectWriter.cpp:223-226` / `ProjectReader.cpp:265-267`) | Third copy lives in ThumbnailSequence selection |
| Output dir + naming | `OutputFileNameGenerator m_outFileNameGen` (MainWindow) | `MainWindow.cpp:739` | root attr `outputDirectory` (`ProjectWriter.cpp:112`) | See F6 and §6.4 |
| Batch-in-progress | derived: `m_batchQueue != nullptr` | `MainWindow.cpp:3427-3429` | not persisted | Clean — no mirrored boolean; auto-mode stage lives only in `m_autoModeStage` (`MainWindow.h:461`) |
| App preferences | `ApplicationSettings` singleton over `QSettings` | `src/core/ApplicationSettings.h:15` | QSettings INI | 25 keys under `settings/` (§6.2) |
| Defaults profile | `DefaultParamsProvider` singleton + `.stp` XML files | `DefaultParamsProvider.cpp:12-34` | `<AppData>/profiles/<name>.stp` | Provider ctor **writes** QSettings (falls back to "Default") — a settings write during singleton init |
| Thumbnails | `ThumbnailPixmapCache` (memory MRU) + PNGs | `ThumbnailPixmapCache.cpp` | `<outDir>/cache/thumbs/*.png` | Disk keyed by source-path MD5 + quality + `v4` scheme; atomic writes |
| Output staleness record | `output::Settings` `PerPageOutputParams` | `output/Settings.h:118-125` | `<output-params>` in project XML | Compared against disk each run (§6.3) |
| PDF import render DPI | **static process-global map** `s_importDpiMap` | `PdfReader.mm:556-573` | **NOT persisted** | Finding F1 |

### 3.2 Duplicated / drifting state — the findings

**F1 — PDF import DPI is lost on project reopen.** `PdfReader::setImportDpi()` is called only from the import dialogs (`MainWindow.cpp:3086`, `AppController.cpp:88,255`). Nothing in `ProjectReader`/`openProject` restores it; `ImageLoader` then renders via `getImportDpi()` default 300 (`ImageLoader.cpp:263-269`, `PdfReader.mm:571`). A project imported at 600 DPI reopens rendering pages at 300 DPI while the project XML still carries the 600-DPI `ImageMetadata` sizes (`ProjectWriter.cpp:186-200`). `output::Settings::m_defaultDpi` (set at `MainWindow.cpp:3150-3154`) is likewise in-memory only. The user-visible symptom (geometry mismatch vs silent rescale) was traced statically, not runtime-reproduced.

**F2 — Finalize "output settings" block is triple-state, partly dead, partly a no-op.**

- `finalize::Settings` holds `m_outputFormat/m_tiffCompression/m_jpegQuality/m_preserveOutput/m_outputPath/m_autoWhiteBalance` (`finalize/Settings.h:131-171`). **None are serialized** — `finalize::Filter::saveSettings` writes only `pictureDetectionSensitivity` + per-page params (`finalize/Filter.cpp:65-72`). Format/quality choices reset on every project reload.
- The UI mirrors format/compression/quality into a third holder, `OutputFileNameGenerator` (`finalize/OptionsWidget.cpp:284-301` → signals → `MainWindow.cpp:1743-1756`).
- **TIFF compression is a no-op**: the TIFF write path (`output/Task.cpp:62-66`) goes through `TiffWriter`, which reads compression from `ApplicationSettings` QSettings (`TiffWriter.cpp:245,248,288,330`). No code syncs finalize's `TiffCompression {LZW=0, Deflate=1}` into ApplicationSettings (zero call sites of the setters outside ApplicationSettings.cpp), `OutputFileNameGenerator::tiffCompression()` has no reader, and the comment at `output/Task.cpp:64-66` claiming the sync happens is **false as of this tree**. The enum values wouldn't even agree (0/1 vs libtiff `COMPRESSION_*` 4/5/8/…).
- `setPreserveOutput`/`setOutputPath` have **zero callers** across `src/`: `m_preserveOutput` is always false, `getEffectiveOutputDir()` always returns the temp dir, and `cleanupTempOutputFiles()`'s preserve check (`MainWindow.cpp:3655`) can never fire. Dead/aspirational state.

**F3 — Page-split layout type is dual-mastered.** `page_split::Settings` stores per-image `LayoutType`; `ProjectPages` independently stores `numSubPages`. Both serialize. Consistency is maintained only by explicit sync calls scattered across `page_split/Task.cpp:205`, `page_split/CacheDrivenTask.cpp:63`, `page_split/OptionsWidget.cpp:296`, `MainWindow.cpp:4658,4693`. Inherited upstream design; drift is possible from any new code path that sets one without the other.

**F4 — White-balance state has two per-page homes.** `finalize::Params::m_forceWhiteBalance` (serialized) vs `output::Settings::m_forceWhiteBalanceDisabled` + `m_manualWhiteBalanceColors` (`output/Settings.h:127-129`, **not** serialized). Finalize's Task copies decisions into output::Settings at process time (`finalize/Task.cpp:91,206,290,333`); `OutputImageParams` embeds the flag for staleness comparison (`output/Task.cpp:212-214`). Manual WB color and per-page force-WB overrides made in the output stage therefore do not survive a project reload unless finalize is re-run. (Statically traced.)

**F5 — `settings/batch_processing_threads` defined ad hoc in two files.** Writer: `SystemLoadWidget.cpp:12,48-50` (also `remove()`s at max). Reader: `WorkerThreadPool.cpp:111`. Same literal string duplicated; not in ApplicationSettings.

**F6 — `outputDirectory` in the project XML can point at a per-machine temp dir.** Unsaved/PDF-import projects use `$TMPDIR/scantailor-spectre-<md5(path)[:8]>` (`finalize/Settings.cpp:248-253`). If saved, that path is serialized (`ProjectWriter.cpp:112`) — and because macOS `$TMPDIR` is usually same-volume, `toRelativeIfSameVolume` stores it **relative** (e.g. `../../../var/folders/…`). On reopen the value is trusted verbatim (`ProjectReader.cpp:31` → `MainWindow.cpp:3309`); nothing recreates the directory (`switchToNewProject` only mkdirs `cache/` inside it, `MainWindow.cpp:708`). On another machine or after a temp purge, every output write fails until the user relinks.

**F7 — color-mode/finalize decision caching is layered but single-owned (acceptable).** Detection result + `detectorSchemaVersion` + `automaticDetection` live in serialized `finalize::Params`; the derived rendering decision copied into `output::Settings` (F4 path) is staleness-checked via `OutputImageParams.matches()`. No third copy found.

### 3.3 Testability seams

- `ProjectWriter`/`ProjectReader` are exercisable without the GUI (`tests/TestProjectPortability.cpp:37,86`, Boost.Test, no QApplication). Good seam. `ProjectFolder` likewise (`TestProjectFolder.cpp:32-79`).
- **But filter-settings round-trip is untestable without widgets**: `saveSettings`/`loadSettings` live on `Filter` classes that construct their `OptionsWidget` in the ctor, so serializing the `<filters>` section requires QApplication. No test covers a full project save→load→save equality; `TestProjectPortability` writes a project with an **empty filter vector**. The 10 filter XML dialects are the biggest untested persistence surface.
- Output generation: `output::Task::process` is entangled with ThumbnailPixmapCache, FilterUiInterface result objects, and Settings; only fragments are tested (`TestPageLayoutOutput`, `MixedOutputDiagnostic`, `TestOutputExposureControls`).
- Round-trip-tested: OCR result XML (`TestOcrResult.cpp`), Zotero sidecar (`TestZoteroLoopSidecar.cpp`), PDF read/export incl. the import-DPI map behavior (`TestPdfReader.cpp:175-216`).

### 3.4 Smaller observations

- `OpenGLSupport.cpp` includes `<QSettings>` but never uses it (dead include).
- `output/Utils.cpp:21-23` `predespeckleDir` is dead code (no callers).
- Relinking must be implemented by **every** path-keyed state holder (`ProjectPages`, all 10 Settings, disambiguator, thumbnail invalidation) — a structural "N places must not forget" pattern; completeness across all 10 implementations was spot-checked, not verified exhaustively.
- `export` settings distinguish "sendToZotero=0 because old build" from an explicit opt-out via a companion marker attr (`export/Filter.cpp:105-112`) — an example of forward-compat care the rest of the format lacks.

### Non-findings

- Batch-processing state is single-sourced (`m_batchQueue` presence); no mirrored boolean flags in filters or the thumbnail sequence; per-`Task` `m_batchProcessing` bools are immutable ctor copies.
- All 10 filter Settings classes uniformly mutex-guarded; no unguarded shared mutable maps in the filter layer.
- QSettings does not shadow per-page project parameters — the app-level/project-level split is clean, with two scoped exceptions (TIFF compression F2; `auto_process/*` machine-global keys that change what a "same" project produces on another machine).
- Thumbnail disk cache never becomes authoritative over project data.

### Unverified

- F1's user-visible symptom needs a runtime repro; the 2.0b2 "PDF import fixes" changelog may touch adjacent code.
- Whether some missed signal path syncs finalize TiffCompression → ApplicationSettings (searched by symbol and key string; none found — medium-high confidence it is a no-op).
- Completeness of `performRelinking` across all 10 Settings implementations.
- Behavior when `outputDirectory` resolves to a non-existent path on open — traced to "nobody obvious mkpaths it"; needs runtime confirmation.
- `page_box` filter serialization not read line-by-line (assumed standard from its save/load symbols).

---

## 4. God objects

Corpus: 458 implementation files (.cpp/.mm), 94,040 lines. 50 non-test files ≥ 400 lines (near-misses: `finalize/Task.cpp` 394, `CylindricalSurfaceDewarper.cpp` 393, `page_split/ImageView.cpp` 380). Entanglement measured as methods whose bodies textually reference a given member. God-object pressure is **concentrated, not systemic**: MainWindow, stage 8 (output), and the defaults dialog.

### 4.1 src/app/MainWindow.cpp — 5,133 lines. The one true god object.

`class MainWindow : public QMainWindow, private FilterUiInterface, private Ui::MainWindow` — the window IS the filter-UI service interface (`MainWindow.h`, 480 lines). **~55 member variables** (lines 410–476), **198 method definitions**, **125 `connect()` calls (79 in the 313-line constructor)**.

Section map (line ranges ±a few):

| Lines | Section | Responsibility |
|-------|---------|----------------|
| 31–208 | anon namespace | Benchmark env-flags, benchmark CSV writer, temp-dir check, export-success dialogs |
| 361–673 | ctor (313 lines) | Widget construction + 79 connects + menu wiring + geometry restore |
| 673–1137 | project switching | `switchToNewProject`, startup panel, thumbnail view setup, save-prompt, sort options |
| 1137–1278 | FilterUiInterface impl | options/image widget swapping, thumbnail invalidation |
| 1278–1622 | selection & navigation | `batchProcessPages`, relinking, 8 goto methods, context menus, autosave |
| 1622–1758 | filter tab logic | `filterSelectionChanged` (55), 8 one-line `switchFilterN`, output dir/format relays |
| 1758–2032 | batch + auto-mode start | `startBatchProcessing` (68), `startAutoMode` (87), `autoModeAdvance` (119-line stage machine) |
| 2032–2362 | auto-mode support | stage timers, timing breakdown, `finishAutoProcess` (158, incl. benchmark CSV + dialog), auto color policy, auto-accept for page split/deskew/content/page-size |
| 2362–2461 | batch control | `startBatchProcessingFrom`, `stopBatchProcessing` |
| 2461–2627 | **`filterResult` (166 lines)** | Central dispatcher: queue feeding, two-pass batch restart, auto-mode advance hook, per-filter summary triggers, beep/alert |
| 2629–2755 | dpi + save | debug toggle, FixDpi dialog, save/save-as |
| 2755–3041 | PDF export | `exportToPdf` (116), `exportToPdfFromFilter` (170) |
| 3041–3330 | project lifecycle | new project, `importPdf` (100), PDF→project import, open/close |
| 3330–3500 | app services | settings dialog, about, Zotero plugin reveal, OOM handler, quit, state predicates |
| 3498–3563 | interactive loading | `loadPageInteractive`, window title |
| 3563–3869 | close/save/cleanup | `closeProjectInteractive` (70), temp-output cleanup + warning, `saveProjectToFolder` (87) |
| 3869–4155 | page manipulation | insert/remove dialogs (112), force layout, per-selection color mode, erase output files |
| 4155–4300 | task factory | `createCompositeTask` (76) + `createCompositeCacheDrivenTask` (61) — hard-wires all 10 filters' Task chains |
| 4300–4542 | window/UI misc | full-bleed toggle, 89-line `keyPressEvent`, docks, thumbnail scaling, goto dialog, autosave timer |
| 4542–5133 | **summary/remediation suite (~590 lines)** | `showBatchProcessingSummary` (80), `showPageBoxSummary` (77), `showContentCoverageSummary` (72), `showPageSizeWarning` (181), plus remediation actions (`forceTwoPageForImages`, `forceSinglePageForImages`, `preserveLayoutForPages`, `disableAlignmentForPages`, jump-to-page ×3) |

**Entanglement:** `m_stages` touched by **50 methods**; `m_thumbSequence` **48**; `m_pages` 37; `m_curFilter` 21; `m_batchQueue` 9; `m_autoModeStage` 7. The batch machinery is a mutual call graph — `startBatchProcessing`/`startAutoMode` → `filterResult` → `stopBatchProcessing`/`autoModeAdvance` → back — coordinating through `m_batchQueue`, `m_autoModeStage`, `m_twoPassBatchInProgress`, `m_twoPassTargetFilter`, `m_batchTimer` and three more timers. `filterResult` alone reads/writes 12+ members and calls into five sections; it is the knot.

**Entangled (extraction would be surgery):** the batch/auto-mode state machine (1758–2627) and the FilterUiInterface widget-swapping + project switching. **Mechanically extractable (narrow inputs):** the summary/remediation suite (~590 lines, ~12 methods); PDF export (2755–3041, ~290 lines of dialog/naming/progress around `PdfExporter.mm`); benchmark CSV writing; the auto-accept helpers (each a self-contained walk over pages writing one filter's Settings); temp-output cleanup (3641–3717). *(Recorded as observed structure, not as a proposal.)*

### 4.2 src/core/filters/output/OutputGenerator.cpp — 3,309 lines

A thin public `OutputGenerator` (4 header members) delegating to a **file-local nested `OutputGenerator::Processor` declared at lines 102–260: 35 members, 34 methods** — the real god object, hidden in the .cpp. Structure: ~900-line anon-namespace helper library (fill zones, margin filling, `findRectAreas` 174 lines, affine transforms, hit-miss); pipeline drivers `processWithoutDewarping` (**261 lines**) and `processWithDewarping` (**365 lines**) — near-parallel copies of the render sequence sharing the same member soup (a render-sequence change must be made twice); `normalizeIlluminationGray` (**260 lines**); binarize ×3 overloads (176+66+45); `buildAutoDistortionModel` (116), `buildMarginalDistortionModel` (90). Member fan-in: `m_status` read in 12 methods, `m_dbg` 11, `m_xform` 10, `m_colorParams` 10 — a "parameter object that ate the algorithm". The anon-ns helpers and the binarize/picture-detection clusters have narrow inputs; the Processor itself is not mechanically splittable without first deciding the pipeline stages' contracts.

### 4.3 src/core/filters/output/OptionsWidget.cpp — 1,795 lines

21 members; ~80 methods, ~60 of them slots. One responsibility (stage-8 options UI) with three mixed mechanics: per-option slot boilerplate; **9 apply-to-selection dialog flows** sharing a template shape (730–1060); and a visibility state machine (`updateColorsDisplay`, **216 lines**). Moderate god object at most — the bulk is inherent to stage 8 having ~40 options.

### 4.4 src/core/filters/output/Task.cpp — 778 lines

**`Task::process` is a single 478-line method** (154–632) covering cache lookup, output-params comparison, regeneration decision, file writing, and UiUpdater construction — the largest single method found outside MainWindow. Entangled with Settings/OutputParams equality semantics.

### 4.5 src/app/DefaultParamsDialog.cpp — 1,124 lines

The issue is not size but **systematic duplication**: it re-implements the show/hide/enable logic of six filters' OptionsWidgets (`colorModeChanged`, `thresholdMethodChanged`, `colorSegmentationToggled`, despeckle slots near-identical to `output/OptionsWidget.cpp`; margin-link logic mirroring `page_layout/OptionsWidget.cpp`). Any new output option must be added in ≥3 places (filter widget, this dialog, DefaultParams model) — the same drift pattern the project already documents for the two `showAboutDialog()`s. `buildParams` is 132 lines reading every widget.

### 4.6 Flagged but not prioritized

- **src/core/ImageViewBase.cpp** (1,074 lines, **44 header members**): interleaves view geometry/transforms, the HQ-pixmap pipeline (6 members + nested task), and interaction plumbing. Inherited nearly as-is from upstream, base of all 10 filter ImageViews, works. High blast radius, low fork-specific churn: flag, don't touch.
- **src/core/ThumbnailPixmapCache.cpp** (1,100): `Impl` *is a QThread* (24 members) mixing LRU policy, disk naming, cross-thread QEvent completion, and a request queue — cohesive purpose, intricate concurrency; treat as a sealed component.
- **src/core/filters/select_content/ContentBoxFinder.cpp** (1,387): `findContentBox` is ~400 lines of sequential phases sharing ~10 locals; cohesive algorithm.
- **src/app/FixDpiDialog.cpp** (740): 4 nested classes, self-contained.

### Non-findings

1. **Binarize.cpp (2,117), Morphology.cpp (1,267), BinaryImage.cpp (1,054), Grayscale.cpp, SeedFill.cpp, SEDM.cpp, ConnectivityMap.cpp, Transform.cpp, ImageCombination.cpp** — cohesive algorithm/data-structure libraries (Binarize is ≈20 independent free functions with uniform signatures and no shared mutable state). Size ≠ god object; splitting would add friction for zero cohesion gain.
2. **ThumbnailSequence.cpp** (1,669) — well-factored facade/pimpl/graphics-items (Impl: 17 members, ~45 methods); dense but single-purpose. Not a restructuring priority.
3. **The dewarping directory** — one algorithm per file, clean.
4. **page_layout/Settings.cpp (1,055), ThumbnailPixmapCache.cpp** — cohesive infrastructure with deliberate internal structure.
5. **The filter architecture itself is healthy**: all 10 stages follow the 7-class pattern with mostly modest file sizes.

### Unverified

- Line ranges are grep-derived; MainWindow ranges 673–1278 and 3330–3563 were characterized from method names + spot reads, not full reads.
- Entanglement counts are textual (a member name in a comment counts); inflation judged small but nonzero.
- Roles for ~15 mid-tier files were verified by class-name extraction + upstream domain knowledge, not line-by-line reading.
- Cross-file fan-in of MainWindow was not measured — as the `FilterUiInterface` implementation, every filter's UiUpdater depends on it; the true blast radius of splitting it includes all 10 filters' Task.cpp.
- `processWithDewarping` vs `processWithoutDewarping` duplication asserted from structure and skim, not a diff; shared fraction unmeasured.

---

## 5. Concurrency

Overall shape: the fork inherits ScanTailor Advanced's disciplined "post events across threads, mutex every Settings object" architecture and largely keeps it intact. Thread creation is centralized in four owners plus GCD in the Objective-C++ layer. No free-floating `std::thread` (only `tests/TestPdfReader.cpp:90,120,223`), no detached thread, no `QThread::terminate()`, no `NSOperationQueue`. Estimated ThreadSanitizer yield on a batch run: **~10–15 distinct reports**, dominated by the DeviationProvider family; a strict human audit adds the QPixmap violation, the iterator invariant, static-executor teardown, cancellation latency, and six nested event loops — **~12 sites worth a ticket**.

### 5.1 Thread creation and pools (complete inventory)

| # | Mechanism | Where | Owner / shutdown | Cancellable? |
|---|-----------|-------|------------------|--------------|
| 1 | `BackgroundExecutor::Impl : QThread` (serial) | `BackgroundExecutor.cpp:25` | **Function-local static** in `ImageViewBase::backgroundExecutor()` (`ImageViewBase.cpp:974-975`); dtor `exit(); wait()` runs at **static destruction, after main()**; `shutdown()` exists but has no callers for this instance | Per-task `QAtomicInt` flags (#7–9) |
| 2 | `WorkerThreadPool` over `QThreadPool` | `WorkerThreadPool.cpp:42` | MainWindow (`unique_ptr`); `~MainWindow` → `shutdown()` → `waitForDone()` (`MainWindow.cpp:678`) | Yes — `BackgroundTask::cancel()` atomic flag |
| 3 | `ThumbnailPixmapCache::Impl : QThread` + its own `QThreadPool` | `ThumbnailPixmapCache.cpp:100,184`; pool sized `idealThreadCount`, throttled to 2 threads/LowPriority during batch (`:630-637`) | pimpl `shared_ptr` held by MainWindow and every LoadFileTask/output::Task; `~Impl` sets `m_shuttingDown` under mutex, `waitForDone`, `quit(); wait()` (`:338-351`) | Generation-counter expiry (REQUEST_EXPIRED); in-flight decodes run to completion |
| 4 | `QtConcurrent::run` thumbnail jobs into #3 | `:654` | with #3 | Expiration only |
| 5 | `QtConcurrent::run` PDF export worker | `MainWindow.cpp:307` (`runPdfExportInBackground`, `:283-340`) | stack `QFutureWatcher` + nested `QEventLoop` | Yes — `std::atomic_bool` polled by progress callback; stops between pages |
| 6 | `RelinkingModel::StatusUpdateThread : QThread` | `RelinkingModel.cpp:39`, lazy start `:349-351` | dtor: exit flag under mutex, `wakeAll()`, `wait()` — clean join | Whole-thread flag |
| 7–9 | `HqTransformTask` (`ImageViewBase.cpp:40-71`), `DespeckleTask` (`output/DespeckleView.cpp:44`), PictureZoneEditor mask task (`PictureZoneEditor.cpp:64`) — all run on #1 | owning widgets, `QPointer`-guarded | with #1 | Yes — `QAtomicInt` each |
| 10–11 | 3 GCD serial queues (metalcontext / metalblur / metalmorphology) + Metal command queue singleton | `MetalContext.mm:29,42`, `MetalGaussBlur.mm:64-70`, `MetalMorphology.mm:16-24` | process-lifetime `dispatch_once` statics; each op a synchronous `dispatch_sync` from the calling worker | n/a |
| 12 | `dispatch_semaphore` `RasterPermit` capping concurrent PDF rasterizations (min of cores, RAM budget, 8) | `PdfReader.mm:182-190`, sizing `:166-179` | static | Blocks `DISPATCH_TIME_FOREVER`; no cancellation while waiting |
| 13 | `dispatch_sync(main)` in `metalLifecycleInit` | `MetalLifecycle.mm:77` | sole caller is `main()` on the main thread, so the deadlock-shaped branch is dead in practice | n/a |

### 5.2 Batch cancel path, traced end to end

`MainWindow::stopBatchProcessing` (`:2413`) → `ProcessingTaskQueue::cancelAndClear` (`ProcessingTaskQueue.cpp:105-114`; cancels only entries already taken — queued-but-not-submitted entries are simply dropped, which is correct) → `BackgroundTask::cancel()` atomic store. Workers check once before starting (`WorkerThreadPool.cpp:65-67`), then run until one of **144 `throwIfCancelled()` sites** throws, caught in `LoadFileTask::operator()` (`LoadFileTask.cpp:75-77`). **The first checkpoint comes after `ImageLoader::load`** (`:61-64`): a cancelled 600-DPI PDF page still performs a full CoreGraphics rasterization. After the load, checkpoints are dense enough that cancellation lands within one processing step. Results of cancelled tasks arrive on the main thread and are discarded by `filterResult` (`:2473-2475`). `~MainWindow` (`:674-678`) cancels both queues then blocks the GUI thread in `waitForDone()` — quit waits for the slowest in-flight page, including the un-cancellable initial decode. `loadPageInteractive` (`:3498-3501`) uses the same machinery: rapid page-switching can briefly have a cancelled task and its replacement for the *same page* concurrently on the pool.

### 5.3 Blocking-primitive inventory (tests excluded)

- **Mutexes: 19** — all 10 filter Settings (page_layout's is cpp-private, the only one not in a header), `ProjectPages`, `ImageSettings`, `FileNameDisambiguator`, `ThumbnailPixmapCache::Impl`, `OutOfMemoryHandler`, `RelinkingModel::StatusUpdateThread`, `PdfDocumentManager` (`PdfReader.mm:161`), static `s_importDpiMutex` (`PdfReader.mm:26`), one `std::mutex` (`ImageLoader.cpp:243`). **~195 lock acquisitions** across 19 files (`output/Settings.cpp` heaviest at 32, `page_layout/Settings.cpp` 24, `finalize/Settings.cpp` 20).
- **Condition variables: 2** — `RelinkingModel.cpp:83` (classic guarded queue) and `ImageLoader.cpp:209` (decode coalescing; predicate and payload written under the same mutex, `notify_all` after unlock — correct).
- **Semaphores: 1** (RasterPermit). **Spin waits: none. Sleeps: 1** — `QThread::msleep(100)` ×11 retry loop for freshly staged PDFs (`PdfReader.mm:117`), which runs on the **main thread** during PDF import.
- **Nested `QEventLoop::exec`: 6 sites**, all reachable on the main thread — `BookLookup.cpp:50,89` (network waits, via the export filter's ISBN button), `ZoteroClient.cpp:147,229,289` (via `MainWindow.cpp:3000`), `MainWindow.cpp:304/329` (PDF export wait). Re-entrancy hazard, not a data race. **`processEvents`: 1** (`MainWindow.cpp:3777`, ExcludeUserInputEvents, project-folder export copy).
- **Atomics: 15 declarations** — cancel flags, `BinaryImage` COW refcount (`imageproc/BinaryImage.cpp:60`), executor start latch, thumbnail `needsReload`, export cancel, the relaxed batch-active counter (`BatchProcessingContext.h:11` — advisory throttle only, relaxed is fine), Metal lifecycle flags, ImageLoader stats counters (relaxed, stats only). No `volatile`-as-sync; no broken double-checked locking (the two CAS latches at `BackgroundExecutor.cpp:102-107` and `MetalLifecycle.mm:69-82` are correct; the `OutOfMemoryHandler.cpp:9-14` comment doubting magic-static safety is an obsolete concern).

### 5.4 Main-thread I/O (GUI blocked)

1. **Project save** — full XML serialization + write, synchronous (`MainWindow.cpp:3717-3718`, close path `:3588`; autosave `:1507` pays the same).
2. **Project open** — `QDomDocument` parse + reconstruction on the GUI thread (`:3288`).
3. **PDF import** — `importPdfFileToProject` (`:3165-3175`) runs `PdfReader::readMetadata` (`PdfReader.mm:367-376`) on the GUI thread: document open (with the 100 ms × 11 retry loop), every page walked for boxes, and `detectEffectiveDpi` (`:300`) decoding image XObjects from up to 5 pages. Large PDFs freeze the UI here.
4. **Export as project folder** — synchronous copy of every original + the output tree with a modal dialog and one `processEvents` (`:3756-3782`).
5. **Thumbnail `loadNow` path** — decodes on the caller (`ThumbnailPixmapCache.cpp:373,402`).
6. **Network with nested loops** — BookLookup / ZoteroClient (above).

On worker threads (confirmed): the entire 10-stage pipeline including decode, TIFF/PNG writes, OCR (`AppleVisionDetector` `performRequests` called synchronously on the worker, `AppleVisionDetector.mm:98`), stage-10 PDF assembly (single ordered CG writer thread, `PdfExporter.mm:195`), thumbnail generation (PDF thumbs rasterized at 72/144 DPI, `ThumbnailPixmapCache.cpp:712-720`), menu-driven PDF export, relink probes.

### 5.5 Shared-unprotected — the findings

1. **`DeviationProvider` — genuine main-vs-worker data race, exercised on every batch run.** `src/core/DeviationProvider.h` is a lazily-recomputed cache: `mutable bool m_needUpdate` (`:43`) rebuilt inside `const` getters `isDeviant`/`getDeviationValue` (`:54-86`, `update()` `:115`) with **no lock of its own**. Four filters embed one in their Settings (deskew, select_content, page_box, page_layout). Writers are fine — `setPageParams` mutates under the Settings mutex, called from worker Tasks after each page. But readers **bypass the Settings mutex**: `Settings::deviationProvider()` returns a bare `const&` consumed by the four `CacheDrivenTask::process` implementations and four `Filter::pageOrderOptions` — and cache-driven tasks run on the **GUI thread** via `ThumbnailFactory::get` (`ThumbnailFactory.cpp:38-42`) on every thumbnail repaint, which during batch is after *every completed page*. The GUI thread runs `update()` — reading `m_perPageParams` through a capture-`this` lambda and rewriting the provider's internal map — concurrently with workers inserting into that map under a mutex the reader never takes. Both a TSan-definite data race and an iterator-invalidation crash window; ~8 racing read sites across 4 filters.
2. **`QPixmap` created on worker threads.** `recreateThumbnail` runs `QPixmap::fromImage` (`ThumbnailPixmapCache.cpp:545`), called from `output::Task` on pool workers (`output/Task.cpp:473,608`). Qt documents QPixmap as GUI-thread-only; the macOS raster backend happens to tolerate it today. First suspect if thumbnails ever corrupt after batch output. (The background *load* path correctly defers `fromImage` to the main thread — asserted at `:860-862`.)
3. **`m_shuttingDown` read without the mutex** at `ThumbnailPixmapCache.cpp:459,495` (written under mutex `:341`; five other reads correctly locked). Benign consequence, guaranteed TSan report.
4. **Cross-thread iterator smuggling** — `backgroundProcessing` captures Boost multi-index `LoadQueue::iterator`s into pool lambdas (`:585-665`) and posts them back in events (`:855-861`). Safe **only** because of the unwritten invariant that IN_PROGRESS items are never erased (`removeExcessLocked` removes LOADED only, `:961-965`) plus the shutdown early-out at `:870`. Correct today; one future "remove item" feature away from a use-after-free.
5. **Static `BackgroundExecutor` destroyed after `main()`** (`ImageViewBase.cpp:974`): its dtor runs `exit(); wait()` at static-destruction time, after QApplication is gone; a still-queued task would `postEvent` against a dead application. Tiny window (owning widgets cancel first), but an ordering hazard with no owner.

### 5.6 Protected / confined (verified)

- All 10 filter Settings mutex-protected; workers copy params out under the lock (e.g. `deskew/Settings.cpp:63-71` returns a `unique_ptr<Params>` copy — the right pattern). `ProjectPages` (15 locked sections), `ImageSettings` (4), `FileNameDisambiguator` (4; `OutputFileNameGenerator` is copied **by value** into tasks — the only shared mutable piece is the mutexed disambiguator). `PdfDocumentManager` LRU + DPI map correct (handles keep evicted docs alive). `DecodedImageCache` coalescing correct. `OutOfMemoryHandler` marshals to main via `QueuedConnection`.
- **No `Qt::DirectConnection` anywhere**; all explicit connection types are `QueuedConnection` (9 sites). Cross-thread delivery is `postEvent` + `customEvent` throughout (BackgroundExecutor, WorkerThreadPool → `taskResult` → `MainWindow::filterResult` at `:557`, ThumbnailPixmapCache, RelinkingModel) — TSan-clean by construction. `ProjectPages` emits `modified` from workers; auto-queued, fine.
- Main-thread-confined by design (correctly unlocked): both `ProcessingTaskQueue`s, MainWindow state incl. `m_autoModeStage`/batch flags, `ThumbnailSequence`, `StageSequence` structure, all OptionsWidgets, RelinkingModel's item vector.
- ThumbnailPixmapCache verdict: **sound under lock** — one mutex covers container, counters, config; `needsReload` correctly atomic with a documented reason; disk writes atomic; weak-ptr completion handlers prevent dangling listeners — with the three blemishes above (findings 2–4). The `loadNow` unlock-reload-relock window can duplicate work but not corrupt.

### 5.7 AccessibilitySafeguard

`src/app/AccessibilitySafeguard.cpp` (installed from `main.cpp`) works around a Qt 6.11.1 macOS **use-after-free** (not a data race): the AX bridge's `accessibilitySelectedChildren` walks `QAccessibleTable`'s `childToId` cache and hands back freed cell objects (`:22-46` documents the disassembly-verified mechanism). The factory hook (`:83-88`) substitutes a plain `QAccessibleWidget` for every `QAbstractItemView`/`QTabBar` (`:66-79`), amputating the crashing selection interface; opt-out env `SCANTAILOR_ENABLE_QT_ITEMVIEW_A11Y=1`. In-process thread implications: none — the AX bridge and the factory both run on the main thread. The concurrency is external (an AX client probing asynchronously while the app mutates item views). Lifetime workaround, correctly scoped, with a stated removal condition (upstream Qt fix).

### Non-findings

- No `std::thread` outside one test file; no detached threads; no `QThread::terminate()`; no `pthread_*`; no cross-thread `DirectConnection`; no `BlockingQueuedConnection`.
- All 10 filter Settings mutexed — none missing.
- No lock-order cycles found: no site holds two audited mutexes at once except Settings→DeviationProvider (one lock), and `PdfReader::setImportDpi` → `ImageLoader::invalidate` is one-directional with no reverse path.
- `RasterPermit` cannot deadlock against the Metal serial queues (no nesting either way).
- `WorkerThreadPool`'s `QSettings` member is main-thread-only.
- Metal morphology's dispatch path is dead code (hard-disabled availability); only Gauss blur reaches Metal, foreground-gated.
- Stale-include red herrings: `StatusBarPanel.h:9` and `ImageViewInfoProvider.cpp:6` include QMutex headers but use no mutex.

### Unverified

- Not every old-style `SIGNAL/SLOT` string connection in MainWindow was resolved for thread affinity (the worker-relevant `taskResult` path was).
- The Metal morphology crash comment ("objc_msgSend during dispatch_sync") was not reproducible from source inspection; MetalContext looks sound; root cause unknown, path currently dead.
- Vision framework internal threading: multiple workers run separate `VNRecognizeTextRequest`s concurrently (`AppleVisionDetector.mm:98,199,592,678`); assumed safe per-request, unverifiable.
- QPixmap-off-main actual behavior on current macOS/Qt 6.11 raster backend (works empirically; contract says no).
- Whether any task can still be queued on the static executor at static destruction — not provable statically.
- `finalize`'s cross-filter use of `output::Settings` (`MainWindow.cpp:4200`) assumed covered by output's 32 lock sites; not every call path walked.

---

## 6. Persistence & side effects

### 6.1 Format inventory (every artifact, writers, readers)

| # | Artifact | Writers | Readers | Notes |
|---|---|---|---|---|
| 1 | Project XML `*.ScanTailor` (version **4**, `version.h.in:8`) | `ProjectWriter::write` — `MainWindow.cpp:3718` (save; also 60 s autosave `:1499-1508` and save-to-folder), `:3588` (close-time comparison copy) | `ProjectReader` via `ProjectOpeningContext.cpp:21-41` | Whole-document rebuild from memory (§6.4). Sections: root attrs (`version`, `outputDirectory`, `layoutDirection`), `<directories>`, `<files>`, `<images>`, `<pages>`, `<file-name-disambiguation>`, `<filters>` with 10 children |
| 2 | `Backup.<project>.ScanTailor` | `MainWindow.cpp:3585-3590` | `compareFiles` then rename-or-remove (`:3603-3620`) | Change detection by serialization: **closing a project writes a full XML even when nothing changed** |
| 3 | Defaults profiles `<AppData>/profiles/*.stp` | `DefaultParamsProfileManager.cpp:92-110` | `:45-90`; `DefaultParamsProvider` ctor | **Shares the project format version number** — bumping the project version silently invalidates all user profiles |
| 4 | QSettings INI (IniFormat forced, `main.cpp:60`; portable override `:62`) | §6.2 | §6.2 | ~40 keys |
| 5 | Output images `<outDir>/<name>.{tif,png,jpg}` | `writeOutputImage` (`output/Task.cpp:55-68,452,557`) | reuse path `:307-330`, CacheDrivenTask, OCR, PDF export | Format chosen by `OutputFileNameGenerator`, **not persisted** (F2) → reopened project regenerates in TIFF |
| 6 | Split sidecars `foreground/`, `background/`, `original_background/` | `output/Task.cpp:523-543`; deleted when mode off `:549-555,458-461` | reuse `:312-328` | Dirs from `output/Utils.cpp:35-45` |
| 7 | Cache sidecars `cache/automask/`, `cache/speckles/` | `output/Task.cpp:570-588` (mkdir deliberately fails if `cache/` absent, comment `:571-575`) | reuse `:332-346` | `cache/predespeckle/` (`Utils.cpp:21-23`) is **dead** — no callers |
| 8 | Thumbnails `cache/thumbs/*.png` | ThumbnailPixmapCache background thread (`:486-491`), `recreateThumbnail` (`:493-560`) | same class | Name = `<base≤180>_<page>_v4_<md5-b64>_q<W>.png` (`:749-788`); atomic writes; alt-path fallback |
| 9 | OCR results | **inside the project XML** — full recognized text + word boxes (`ocr/Filter.cpp:66-93`) | same | No sidecar. Staleness = schema + dims + language + flags + output file **size and mtime** (`ocr/Task.cpp:160-171`) |
| 10 | Exported PDF | `PdfExporter.mm` CGPDFContext incremental (`:192-260`), staged `QTemporaryFile` + backup-rename dance (`:546-575`) | external | Title/author from `BookMetadata`, serialized in project XML (`export/Filter.cpp:78-88`) |
| 11 | Zotero loop sidecar `.zotero-loop.json` | **read-only** (written by the Zotero plugin) | `ZoteroLoopSidecar.cpp:20,115-123`; armed at project open (`MainWindow.cpp:751-753`) | Validated, path-resolved |
| 12 | Zotero delivery | HTTP POST to `http://127.0.0.1:23119` (`ZoteroClient.cpp:23`) | — | External side effect |
| 13 | Book metadata lookup | none | HTTPS GET openlibrary.org / googleapis.com (`BookLookup.cpp:288-291`) | Sends ISBN only |
| 14 | Temp output dir `$TMPDIR/scantailor-spectre-<hash>` | #5–#8 when unsaved | same | **Recursively deleted** on close (`MainWindow.cpp:3641-3689`), guarded by prefix check (`:249`) + optional warning dialog |
| 15 | Project folder layout (`originals/`, `cache/`, `output/`) | `ProjectFolder` (`ProjectFolder.h:12-17`) | `findProjectFile`, `isValidProjectFolder` | Save-to-folder copies inputs — a second on-disk copy of sources |
| 16 | PDF import | none — pages rendered on demand via CoreGraphics | | **Non-finding:** no extracted temp images, no sidecar |

No security-scoped bookmarks or NSUserDefaults-style persistence exist in any `.mm` file (grep: 0 hits).

### 6.2 QSettings key inventory

15 files touch QSettings; ~40 keys:

- `settings/…` × 25 (enable_opengl, auto_save_project, color_scheme, **bw_compression, color_compression** — no in-app writer, hand-edit only, yet read by TiffWriter at every image write —, black_on_white detection ×2, deviation coef/threshold ×6, thumbnail sizes ×2, single_column display, language, units, current_profile, selection_canceling_question, jpeg_output, jpeg_quality, temp_cleanup_warning, pdf_recommended_name).
- `settings/batch_processing_threads` — the F5 duplicated literal.
- `mainWindow/maximized`, `mainWindow/nonMaximizedGeometry` vs **`main_window/external_alarm_cmd`** — inconsistent group naming; the latter has **no writer** (hand-edit only) and its value is passed to `std::system()` (`MainWindow.cpp:2585-2591`), i.e. arbitrary command execution from an INI value (inherited from upstream).
- `project/lastDir`, group-less `lastInputDir`, `project/recent` (read-validate-**rewrite** during `NewOpenProjectPanel` construction, `NewOpenProjectPanel.cpp:96-99` — merely showing the panel mutates state).
- `auto_process/{color_handling,redetect_color,include_ocr,fast_ocr,multilingual_ocr,skip_ocr_language_correction}` (`AutoProcessDialog.cpp:25-42,88-93`).
- `margins/leftRightLinked`, `margins/topBottomLinked` (`page_layout/OptionsWidget.cpp`).
- `CollapsibleGroupBox/<objectName>/{checked,collapsed}` — written in the **destructor** (`CollapsibleGroupBox.cpp:217-233`), read in `showEvent`.

### 6.3 Output staleness contract (`output/Task.cpp:194-301`)

Stored `OutputParams` compared against regenerated params (all processing knobs incl. WB), zone sets, source identity, and per-file identity of out/foreground/background/original_background/automask/speckles. **File identity = size only** — `OutputFileParams::matches` has the mtime comparison **commented out** (`output/OutputFileParams.h:41-43`); a same-size replacement passes as current. OCR, by contrast, checks size **and** mtime — two different notions of "same file" in one pipeline. On any write failure stored params are removed (`:590-591,463-464`) so the page reprocesses — fail-safe direction correct. `deleteMutuallyExclusiveOutputFiles` (`:637+`) removes sibling-subpage outputs — a filesystem side effect beyond the page being processed.

### 6.4 Round-trip behavior and the version wall

`ProjectWriter::write` builds a fresh `QDomDocument` from memory (`ProjectWriter.cpp:105-138`); **any element or attribute a future or forked version added is silently discarded on the next save**. No migration layer: `ProjectReader.cpp:26-29` requires `version.toInt() == 4` exactly — older *and* newer projects are rejected (one attr-level shim: `multiPage`, `:149-153`). Per-page decode failures are tolerant, not fatal: reader and all filter `loadSettings` `continue` past malformed entries (e.g. `output/Filter.cpp:86-99`), leaving pages on defaults; the whole open fails only on version mismatch, missing top-level section, or zero valid images. **The silent-then-destructive combination** — malformed page reverts to defaults, next save permanently erases the possibly-recoverable original — is the real risk.

### 6.5 Portable relative paths (2026-07-24 feature) — healthy

`toRelativeIfSameVolume` (`ProjectWriter.cpp:70-103`) converts same-volume paths to project-relative on write (applied to `outputDirectory` `:112` and `<directory path>` `:146`); `ProjectReader::resolvePath` (`:69-74`) resolves on read; absolute paths pass through (legacy compat). Ownership clean and symmetric; covered by real round-trip tests (`TestProjectPortability.cpp:53-129`). The weak spot is its interaction with temp output dirs (F6: a `$TMPDIR` path stored *relative* through `/var/folders`).

### 6.6 I/O in UI lifecycle and event handlers

| Where | I/O | Severity |
|---|---|---|
| 60 s autosave timer → `autoSaveProject` (`MainWindow.cpp:385-386,1499-1508,4492-4495`) | Full project serialize+write on the UI thread | Fires mid-batch; per-filter Settings individually mutexed but the document is **not a consistent snapshot** across filters while workers mutate them |
| `closeProjectInteractive` (`:3584-3620`) | Full `Backup.*` XML written just to diff | By design; "close without changes" still does two writes + full compare |
| closeEvent/quit (`:827-831,977-990`) | QSettings geometry writes | Fine |
| `timerEvent` (`:963`) + batch completion (`:2585-2591`) | `external_alarm_cmd` → `std::system()` | See §6.2 |
| `CollapsibleGroupBox` showEvent/**destructor** | QSettings read/write per group box at every options-panel teardown | Odd but harmless |
| `NewOpenProjectPanel` construction | Recent-projects read + rewrite | Startup-path disk+settings I/O |
| `cleanupTempOutputFiles` (`:3641-3689`) | `QDir::removeRecursively()` — permanent deletion | Guarded by `scantailor-spectre-` prefix check + warning dialog; deliberate |
| ThumbnailPixmapCache | All PNG I/O on its background thread | **Non-finding:** paint-path requests queued, not synchronous |

### Non-findings

1. No Apple-side persistence (bookmarks/NSUserDefaults) in `.mm` files; PDF import extracts no temp images.
2. No `tr()`/localized strings reach any serialized format — all enum↔string maps are hard-coded English tokens (`AutoManualMode.cpp:6-31`, `"LTR"/"RTL"`, `creatorRoleToString`); finalize/export write numeric enum ints — stable but opaque.
3. Thumbnail cache never authoritative over project data; writes atomic; names content-addressed (path MD5 + quality + `v4`).
4. QSettings does not shadow per-page project state (two scoped exceptions: F2 TIFF compression; `auto_process/*`).
5. Project open is per-page fault-tolerant (traced through the reader and all filter `loadSettings`).

### Unverified

- Autosave-during-batch torn-snapshot risk: theoretical from locking structure; not reproduced. (On reload the staleness contract forces reprocess of a disagreeing page, so ordinarily self-healing; a crash right after such a save is the case to think about.)
- Whether output writes fail loudly or confusingly when the serialized `outputDirectory` doesn't exist (F6) — needs runtime confirmation.

---

## 7. Naming & navigation

### 7.1 Name strata — four names, each live in a different layer

| Name | Where it lives | Status |
|---|---|---|
| **scantailor-weasel** | Repo dir name; `namespace weasel` (`src/core/weasel/`, 10 files); one **user-visible UI string** | Legacy codename, still shipping in UI |
| **ScanTailor Spectre** | CMake `project()` (root `CMakeLists.txt:10`), bundle `com.scantailor.spectre` (`packaging/macos/Info.plist.in:12`), README, About dialogs, most new-file headers | The product name |
| **ScanTailor Advanced** | Copyright headers on ~2024-era files, `Info.plist.in:30` copyright, **all of `packaging/macos/BUILD.md`**, `scantailor.desktop` (`Name=ScanTailor Advanced`), CPack vendor `4lex4 <4lex49@zoho.com>` (`CMakeLists.txt:407`) | Upstream; mostly correct GPL attribution, but BUILD.md and CPack/desktop entries are stale product identity |
| **scantailor** (plain) | Executable target, `APPLICATION_NAME "scantailor-spectre"` (config.h), `scantailor_*.ts` (8), `scantailor.icns`, CPack executables | Internal build vocabulary; a harmless fourth stratum |

Specific findings:

- **"Weasel" is user-visible**: `src/app/AutoProcessDialog.ui:58` — "Recognize text from Weasel's processed page images after Output." The only user-visible weasel string (97 total hits in `src/`; the rest are the live `namespace weasel` panel-bridge code).
- **QSettings identity ≠ bundle identity**: `main.cpp:57-58` sets app and org name both to `"scantailor-spectre"` (`config.h.in:12` defines `ORGANIZATION_NAME APPLICATION_NAME`) while the bundle is `com.scantailor.spectre`. Any future rename must migrate both or orphan user settings.
- **`src/core/weasel/` is misnamed by function** — it is the WebEngine options-panel infrastructure; nothing at tree level points a newcomer hunting for Photo Adjustments to it.
- **Icon/artifact clutter**: three tracked icon generations (`scantailor-spectres.icns` at root + two in `src/resources/`); a loose untracked 2.0a35 DMG at repo root.

### 7.2 Feature findability (5 shipped features)

| Feature | Where a newcomer looks | Where it lives | Verdict |
|---|---|---|---|
| PDF import | `filters/` or an import dir | `core/PdfReader.mm` + `PdfReadError.mm` + `PdfMetadataLoader.cpp` (core, flat) + `app/PdfImportDialog.cpp` + wiring in AppController/MainWindow | Findable with grep, not from the tree |
| OCR stage | `filters/ocr/` ✓ | `filters/ocr/` (UI/task), but the engine is `core/AppleVisionDetector.mm` (shared with 3 other stages) and the macOS-26 path is `core/SpectreDocumentOCRBridge.swift`, built by a custom command (`src/core/CMakeLists.txt:160-192`); nothing in `filters/ocr/` points at the bridge | Half-findable |
| Auto color-mode (Finalize) | `filters/finalize/` ✓ | `finalize/Task.cpp` → `core/LeptonicaDetector` + `core/PhotoFrameDetector`; policy in `finalize/AutoColorModePolicy.h` | Findable, with a trap: `core/ImageTypeDetector` and AppleVisionDetector *look* like the color detector but are not |
| Multi-select batch | a batch/ or selection/ dir | `MainWindow.cpp` + `ThumbnailSequence.cpp` + `PageSelectionAccessor`; `selectionIndicatorLabel` in 8 of 10 filter OptionsWidgets (absent: ocr, export) | Not findable from the tree |
| Auto Mode | an automode/ dir or filter | `MainWindow.h:451-461` `enum AutoModeStage` (7 stages PAGE_SPLIT→OCR) + state machine inside `filterResult()`; dialog `app/AutoProcessDialog.*` | Not findable from the tree |

Score: 1 clean hit, 2 partial, 2 invisible. The filter-stage skeleton is excellently navigable (10 dirs, uniform 7-class pattern, namespace == directory); everything the fork added (import, Auto Mode, batch summaries, Zotero loop) concentrates in MainWindow.cpp and flat `src/core/` files.

### 7.3 Name collisions and disambiguation

- Each filter dir defines `Filter`, `Task`, `CacheDrivenTask`, `Settings`, `OptionsWidget`, `ImageView`, `Thumbnail` in a namespace matching the directory. Each `Filter.h` also forward-declares the *next* stage's namespace — the convention holds but is grep-hostile.
- **The `export` triple-name**: directory `filters/export/`, namespace `export_` (reserved keyword), CMake target `export_`. Three spellings for one stage; `CLAUDE.md:94` documents the directory as `export_/`, which is wrong.
- **One filter broke the UI-file convention**: 9 filters use `OptionsWidget.ui`; ocr uses `OcrOptionsWidget.ui` (and uniquely prefixes `OcrResult`). Two standalone test targets must manually inject `*_autogen/include` paths to reach generated UI headers — the collision-prone design leaks into the test build.

### Non-findings

- Filter naming convention holds for all 10 stages (sole spelling exception: export/export_).
- No `.DS_Store` tracked. Version consistency: `version.h.in` 2.0b2 == README 2.0b2 == last release tag; timestamp mechanics work as documented.

### Unverified

- (None specific to naming beyond the doc items in §8.)

---

## 8. Additional inventory

### 8.1 Build health

- **Toolchain**: CMake ≥3.16, C++17, `CMAKE_OSX_DEPLOYMENT_TARGET` forced to **15.0** if undefined (`CMakeLists.txt:16-18`), `LSMinimumSystemVersion 15.0`. The macOS-26 Swift OCR bridge (`libSpectreDocumentOCRBridge.dylib`) is built via raw `xcrun swiftc` **only when the installed SDK ≥ 26.0** — build output differs by machine SDK with no CMake option to control it.
- **Fresh-clone workarounds (the tribal list)** — a fresh clone does not configure with the documented commands alone:
  1. `PKG_CONFIG_PATH=/opt/homebrew/lib/pkgconfig` required for Leptonica (`pkg_check_modules(LEPTONICA REQUIRED lept)`) — documented **only** in BUILD_LOG.md entries and private session memory; absent from CLAUDE.md, README, SIGNING.md, BUILD.md. The largest doc/reality gap for a newcomer.
  2. `-DCMAKE_PREFIX_PATH=$(brew --prefix qt6)` — documented.
  3. Repo-root symlink `ScanTailor Spectre.app -> build/…` (per-machine convention).
  4. `CLANG_MODULE_CACHE_PATH` export in SIGNING.md's build step (undocumented why).
  5. `scantailor_bundle` hard-depends on the tracked `ScanTailor Spectre Readme.pdf`; regeneration requires pandoc + headless Chrome at a hardcoded `/Applications/Google Chrome.app` path.
  6. `scantailor_bundle` hard-depends on `zotero-plugin/dist/st-spectre-loop.xpi` (a **committed binary**, 9.9 KB) matching `manifest.json`'s version — a stale XPI fails the bundle build until `build.sh` is rerun.
- **Generated-file mechanics: sound.** `version.h.in` → `build/version.h` via an ALL custom target; the version string is regex-parsed out of `version.h.in` at configure time (`CMakeLists.txt:277-309`); `config.h` gets a regex-surgery pass to comment out `ENABLE_DEBUG_FEATURES` — fragile but working.
- **Two deploy paths**: `scantailor_bundle` (`src/app/CMakeLists.txt:184-212`: wipe, macdeployqt, `fix-bundle-libs.sh`, OCR bridge + README PDF + validated XPI, ad-hoc sign via `sign-adhoc.sh` which knows the QtWebEngineProcess helper) is the real one; the older `SCANTAILOR_MAC_BUNDLE_POST_BUILD` option (default OFF) duplicates part of it with a `--deep` codesign — a second, worse path kept alive.
- **`packaging/macos/create-dmg.sh` vs SIGNING.md**: they agree — SIGNING.md:23-27 explicitly declares the script non-compliant (wrong keychain profile `notarytool` vs `notary` at script line 20, `--deep` signing line 73, untimestamped DMG line 119). The script has not been fixed; a documented landmine. **BUILD.md contradicts both** by recommending it unconditionally.
- **Dead platform scaffolding in a macOS-only project**: `CMakeLists.txt:12-14` hard-errors on non-Darwin, yet 16 `WIN32` references remain (~150 lines: DLL globs, NSIS, windeployqt), plus `cmake/CopyToBuildDir.cmake` and `cmake/AddDynamicLibraryLocations.cmake` (WIN32-only, unreachable), Linux `.desktop`/mime rules (`:386-394`), windres logic. None can execute.
- **Repo hygiene**: `build/` 890 MB (ignored); `dist/` 2.4 GB with 21 tracked files including 6 DMGs (**deliberate**, per the owner's private-origin artifacts policy); **`tmp/` 622 MB with 707 git-tracked files** — a complete extracted 2.0b1 app bundle (QtWebEngine framework included) plus its `dmg-root/` twin, apparently accidental; `work/` 4.7 MB of agent brief/status files; `.gitignore` still carries rules for a `pizza/` directory that no longer exists. BUILD_LOG.md is 3,762 lines / 274 KB, append-only.

### 8.2 Test targets — what each exercises

| Target (CTest?) | Exercises | Live or dead path? |
|---|---|---|
| `core_tests` (yes, 18 suites) | LeptonicaDetector, AutoColorModePolicy, PhotoAdjustments (weasel), PdfExporter/PdfReader/PdfReadError, OcrResult, PictureRegionMask/PhotoFrameDetector, PlatePrior, ProjectFolder/Portability, ZoteroLoopSidecar, batch context, filename ordering | All live; `TestPdfExporter.cpp` pins the live CG path |
| `zotero_client_tests` (yes) | ZoteroClient connector protocol | Live |
| `export_options_layout_tests`, `output_exposure_controls_tests` (yes, offscreen QPA) | export_/output options UI layout | Live |
| `imageproc_tests` (yes, 17 suites) | CPU imageproc; `TestMorphology.cpp:678` claims coverage of "accelerated implementations (vImage/Metal)" but Metal morphology returns false, so **only the CPU path runs**; Metal paths are untested by anything | Live CPU |
| `math_tests` (yes, 3 suites) | Splines/matrices | Live |
| **5 diagnostic executables, no CTest registration** (`color_detection_diagnostic`, `geometry_diagnostic`, `mixed_output_diagnostic`, `margins_output_diagnostic`, `full_pdf_bw_diagnostic`) | One-off investigation harnesses in `src/core/tests/`, built on every full build | Session leftovers as permanent build cost |

No test exercises superseded code — the inverse problem exists: dead code (`ImageTypeDetector`, Metal morphology, `exportWithQt`) has no tests either, so nothing would notice its removal *or* its rot. No `app/` tests exist at all (MainWindow, ThumbnailSequence, batch/auto-mode: zero coverage).

### 8.3 Third-party dependencies

| Dependency | Constraint | Status |
|---|---|---|
| Qt6 | ≥6.4; Core, Gui, Widgets, Xml, Network, LinguistTools, OpenGL, Svg, OpenGLWidgets, **WebEngineWidgets, WebChannel** | Live. WebEngine is REQUIRED solely for the weasel Photo Adjustments panel — the heaviest possible dependency (~200 MB QtWebEngineCore in every bundle) for one options panel |
| Boost | ≥1.60; unit_test_framework, prg_exec_monitor | Live; 66 non-test files use header Boost including **legacy idioms** (`boost/lambda`, `boost/bind`, `boost/foreach`, `boost/mpl`) under `-std=c++17` |
| JPEG, ZLIB, PNG, TIFF | unversioned REQUIRED | Live |
| Leptonica | pkg-config `lept`, unversioned | Live (LeptonicaDetector) |
| **LibHaru** | optional, `cmake/FindLibHaru.cmake` | **Phantom**: found, "PDF export will use optimized compression" printed, `HAVE_LIBHARU` defined, linked into core (`src/core/CMakeLists.txt:203`) — **zero source references to `HPDF`/`hpdf`/`HAVE_LIBHARU`**. The live exporter is `PdfExporter.mm` (CGPDFContext); its `QPdfWriter` fallback (`:556-565`, `exportWithQt` `:451`) fires only "if CoreGraphics not available" — effectively never on macOS-only, so near-dead too |
| Apple frameworks | Metal, Foundation, AppKit, Vision, Accelerate, CoreGraphics, CoreText | Live; the 4 `VNRecognizeTextRequest`/`VNDetectRectanglesRequest` sites in `AppleVisionDetector.mm` are the APIs Apple is superseding in macOS 26 — already mitigated by the SDK-26 Swift bridge design |

No vendored `3rdparty/` directory exists.

### 8.4 Deprecated-API sweep (quantified)

0 hits for `QDesktopWidget`, `QRegExp`, `QTextCodec`, `Q_FOREACH`, `qrand`, Qt5 module names; 0 deprecation pragmas in src. The Qt side is clean Qt6. The only future-risk APIs are the 4 VN* Vision sites (soft-deprecated; fallback exists).

### 8.5 Documentation vs code — every contradiction found

| Doc claim | Reality | Verdict |
|---|---|---|
| `CLAUDE.md:3` "requires macOS 11+" | Deployment target 15.0 everywhere | Wrong |
| `CLAUDE.md:113` "Base class for all **8** filters" | 10 filters registered (`StageSequence.cpp:31-59`) | Wrong (pre-finalize/ocr era) |
| `CLAUDE.md:94` stage-10 dir `export_/` | Directory is `export/` | Wrong path |
| CLAUDE.md "Change color detection → AppleVisionDetector.mm" | Color detection is LeptonicaDetector (+PhotoFrameDetector); AppleVision does rectangles/OCR | Wrong pointer (CLAUDE.md:145 gets it right two sections earlier) |
| CLAUDE.md "Modify PDF export → PdfExporter.cpp" | File is `PdfExporter.mm` | Wrong extension |
| `CLAUDE.md:173` brew line includes libharu | Unused by code | Misleading |
| CLAUDE.md "Current version: check CMakeLists.txt" | Version lives in `version.h.in:6` (2.0b2); CMakeLists parses it from there | Misleading |
| CLAUDE.md build commands | Omit the required `PKG_CONFIG_PATH` | Incomplete |
| `CLAUDE.md:126` "Metal … currently disabled" | Only morphology disabled; GaussBlur live | Half-wrong |
| `packaging/macos/BUILD.md` (whole file) | Names the product ScanTailor Advanced, clones `4lex4/scantailor-advanced`, omits Leptonica, praises libharu ("10-20x smaller files"), recommends create-dmg.sh and `--deep` signing | Stale end to end; contradicts SIGNING.md |
| README.md summary-dialog section: "Stage 4 (Select Content)", "Stage 5 (Margins)", "stage 6, Finalize" | README's own table: Select Content=5, Margins=6, Finalize=7 | Internally inconsistent (off by one) |
| `compatibility.md` "Goal: enable macOS 11 support" | Target 15.0; goal abandoned | Stale (and the likely source of CLAUDE.md's "11+") |
| `TODO.md` "Current Task: re-enable Metal", Phase 1 = create lifecycle observer | `MetalLifecycle.mm` already implements it; GaussBlur re-enabled | Partially done, not checked off |

**Accurate for the record**: `ARCHITECTURE.md` (ten stages, correct `export` dir — the best onboarding doc), `SIGNING.md` (self-consistent, honestly flags create-dmg.sh), README versions/OS, `docs/zotero-loop.md`. Root-level doc sprawl: 14 markdown files at repo root, of which 5 are stale session artifacts (`HANDOFF.md` 2026-04-16, `SESSION_2026-04-10_HANDOFF.md`, `CHANGELOG_SESSION.md`, `PERFORMANCE_INVESTIGATION_PROMPT.md`, `UI_REDESIGN.md` — the last referencing `/tmp/` mockups that no longer exist), plus superseded `compatibility.md` and historical `AUDIT-2.0b1.md`.

### Non-findings

- No Qt5-isms or deprecated Qt APIs (§8.4).
- All CTest-registered suites pin live code paths.
- No secrets or personal data noticed in tracked build/packaging files (not an exhaustive secret scan; out of scope).

### Unverified

- Whether a genuinely fresh clone configures with only `PKG_CONFIG_PATH` + `CMAKE_PREFIX_PATH` added (no build was run per the audit's ground rules); BUILD_LOG.md:1947 records SDK-drift reconfigure failures suggesting sensitivity beyond those two.
- Whether the `QPdfWriter` fallback is reachable on any supported macOS 15+ configuration (appears dead).
- Whether Homebrew qt still bundles WebEngine on all target machines (the Studio Mac is on an older Qt per session memory).
- Exact deprecation annotation of ObjC `VNRecognizeTextRequest` in SDK 26 (Apple's Swift direction is clear; the installed SDK headers were not checked).
- Whether the tracked `tmp/release-2.0b1-*/` bundle was committed deliberately as a release archive or by accident.

---

## 9. Blast-radius question list

Merged and deduplicated from all five reports; grouped by finding. Each is the question that must be answered before anyone acts on that finding. Where an auditor already traced the answer, it is stated.

### Breaking the core↔filters SCC

1. **The first cut is `core/StageSequence.h:12-21` and `core/DefaultParams.h:6-14`.** What consumes `DefaultParams` serialization (project files? `.stp` profiles on disk?) — can the six filter option types it embeds move down into core without changing any persisted XML schema?
2. **`output/Settings.h` (26 includers) and `output/ColorParams.h` (19) span finalize, ocr, export, core, app.** Which parts are per-page persisted state (project-file compatibility risk) vs runtime-only, before any attempt to hoist them into a shared model layer?
3. **How much of core's PUBLIC surface do the filters actually use?** Any future modularization (a CLI target, faster test links) has to break the circular linkage first.
4. **Is the pipeline-tail direction flip (ocr→output, export→ocr, vs the forward chain 1→8) an ordering artifact or a real data dependency** (ocr consumes output's rendered image)? Decides whether the chain can be made uniform or needs an explicit pipeline coordinator.

### MainWindow and the batch/auto-mode machinery

5. **Is there any consumer of the filter Task/CacheDrivenTask constructors other than MainWindow and the neighboring filter in the chain** (batch CLI, tests, future automation) that a constructor-signature change would break? MainWindow's 50 filter-internal includes make it the single choke point.
6. **If a BatchController is extracted, what owns the `FilterResultPtr → updateUI(this)` call** — can `FilterUiInterface` be handed to a controller without every filter's UiUpdater (10 files) changing its assumptions about which widget-swap side effects happen during batch? `MainWindow::filterResult` is the single convergence point for interactive tasks, manual batch, two-pass batch, and auto mode.
7. **Which invariants does `stopBatchProcessing(…, resetAutoMode=false)` actually guarantee, and are they tested anywhere?** Auto-mode state (`m_autoModeStage` + 4 timers + 3 flags) is saved/restored across it; per session memory this was already a re-entrancy trap.

### Output stage

8. **Do the duplicated `processWith[out]Dewarping` drivers behaviorally diverge today** (e.g. order of fill-zone application vs splitting), and is any divergence a bug or a load-bearing difference users depend on? Must be answered before unifying them.
9. **Is there any characterization coverage at all for ImageViewBase's zoom/pan/HQ-rebuild behavior?** Traced answer: **none found** under `src/*/tests`. Any change to its transform/focal-point protocol propagates to all 10 filter ImageViews plus the interaction framework.

### Persistence

10. **Does a bad per-page params element abort project open?** Traced answer: **no** — the reader and filters `continue` past it; the page silently reverts to defaults, and the *next save permanently erases* the malformed-but-possibly-recoverable data. The silent-then-destructive combination is the real risk.
11. **What happens to a v4 project opened by a hypothetical v5 build, or vice versa?** Traced answer: **hard reject in both directions, no migration — and defaults profiles die too** (shared version constant, `DefaultParamsProfileManager.cpp:78,96`). Any format change is a compatibility wall; is that accepted policy?
12. **Project saved on a temp output dir, reopened after reboot or on another machine** (F6): the stored path (possibly relative through `/var/folders`) doesn't exist and `cache/` mkdir fails by design — do output writes fail loudly per page or produce a confusing relink prompt? (Unverified; needs a runtime check.)
13. **Is size-only output staleness intentional?** The mtime check is deliberately commented out (`output/OutputFileParams.h:41-43`); an externally modified same-size TIFF is served as current to PDF export (OCR would catch it via mtime). Upstream inheritance — keep or align the two file-identity notions?
14. **Two windows, one PDF**: two projects created from the same source path share the same md5-derived temp output dir (`finalize/Settings.cpp:248-253` hashes path only); closing one recursively deletes the other's output. Not runtime-verified — does the second window survive?

### Concurrency

15. **If the DeviationProvider race fires during a 500-page batch, what breaks?** Worst case: an unordered_map rehash on the GUI thread racing a worker insert — heap corruption in a Settings object shared by every page, taking the session down. Does any beta crash telemetry show frames in `DeviationProvider::update` / thumbnail paint?
16. **App-quit latency**: `~MainWindow` blocks the GUI thread in `waitForDone()` after a cancel whose first checkpoint is *after* full image decode. On a 600-DPI PDF with 8 in-flight pages, what is the measured worst-case quit hang — long enough for "not responding" and a force-kill mid-write of output TIFFs?
17. **Cancelled-task/replacement overlap**: when `loadPageInteractive` cancels and immediately resubmits the same page, both tasks can be live on the pool; the cancelled one may be mid-write to files the new one reads. `AtomicFileOverwriter` protects thumbnails — do the TIFF output writes in `output::Task` have the same atomicity?
18. **Thumbnail-pool starvation inversion**: during batch, pool #3 drops to 2 low-priority threads keyed off the relaxed global counter. If a batch task ever synchronously waits on a thumbnail, that is a priority-inversion deadlock candidate — is that path reachable?

### Dependencies and identity

19. **If WebEngine were dropped** (Photo Adjustments rebuilt native), what else silently relies on WebEngine/WebChannel being on core's PUBLIC interface? Known: `main.cpp:29`'s comment says the OpenGL/Metal backend choice is made *because of* WebEngine; `sign-adhoc.sh`'s helper-app handling; bundle size. And how much of `TonalCurve`/`PhotoAdjustments` math is UI-independent and portable? (Traced: both are pure data/LUT code with no UI includes.)
20. **Any rename touching `APPLICATION_NAME` silently relocates every user's QSettings** (org `scantailor-spectre` vs bundle `com.scantailor.spectre`) — which shipped preferences (Zotero opt-outs, defaults profile pointer, color scheme) would be orphaned, and is there a migration path?
21. **Deleting the dead WIN32/Linux CMake scaffolding** (~200 lines + 2 cmake modules + unix resources): does any intent to restore cross-platform builds exist, or is the macOS-only commitment final in practice?
22. **The SDK-26-conditional Swift OCR bridge means two machines produce different bundles from the same commit** — acceptable for release reproducibility, or should the bridge become an explicit CMake option gated in SIGNING.md's preflight?

---

## 10. Product decisions

Merged and deduplicated from all five reports. Each is a question with options and trade-offs — **explicitly not decided here**.

1. **Metal morphology.** Hard-disabled (`MetalMorphology.mm:25-29`, unreproduced objc_msgSend crash TODO) but fully compiled, shader-bundled, and untested; docs say all Metal is off while Gauss blur is live. Options: (a) root-cause the crash and re-enable (TODO.md's plan, Phase 1 already done); (b) delete `MetalMorphology.*` + shader and keep vImage/CPU + blur; (c) freeze as-is. Currently silently (c) — and whichever way, CLAUDE.md/TODO.md should be corrected to match reality.
2. **libharu.** Options: (a) remove find/link/docs — the code already voted (zero references); (b) actually implement HPDF compression as once advertised. Currently a phantom dependency that misleads the build output and docs.
3. **ImageTypeDetector.** Zero users, superseded by LeptonicaDetector, no tests. Delete, or document why it is retained.
4. **QtWebEngine for one panel.** The ~200 MB dependency exists for the Photo Adjustments panel alone (plus a shipped-but-unreferenced `fix_orientation.html`). Options: (a) WebEngine panels are the strategic direction for filter UIs — the PUBLIC link and bundle weight are accepted costs, more panels should migrate, and `fix_orientation.html` is planned work; (b) it was an experiment — a native QWidget rewrite of one panel removes the heaviest dependency in the app, and the stray HTML is deleted; (c) keep the stray HTML as a template either way. Which direction is intended?
5. **The user-visible "Weasel" string** (`AutoProcessDialog.ui:58`). Rename to Spectre (one-line .ui change + retranslation) or keep the codename as an easter egg. Ships to users today.
6. **DefaultParamsDialog hand-mirroring.** (i) keep mirroring by hand and accept ≥3-place edits per new option; (ii) generate/share option panels between the dialog and the filters' OptionsWidgets; (iii) drop rarely-used options from the defaults dialog so it stops mirroring everything. Underlying question: which options must genuinely be settable as global defaults?
7. **The four post-batch summary dialogs** (page split, page box, content coverage, page size) live in MainWindow (~590 lines) and encode remediation policy. Permanent product surface (worth a proper coordinator + settings model) or evolving experiment (keep cheap, don't invest yet)?
8. **Benchmark/auto-process instrumentation** (env-var gates, CSV writer, timing breakdown in `finishAutoProcess`) ships inside MainWindow. Hidden developer feature to be stripped from release paths, or supported feature — which decides extraction vs deletion?
9. **Finalize output block (F2).** (i) wire format/compression/quality into ApplicationSettings and serialize per-project; (ii) delete the dead preserve-output + compression combo and let QSettings be openly authoritative; (iii) make the project XML authoritative and drop the QSettings compression keys. Currently the UI implies per-project control it does not have.
10. **PDF import DPI (F1).** Serialize per-source-file render DPI into the project XML, or re-derive from stored ImageMetadata on open. Someone must own this value; today no one does after process exit.
11. **The version wall.** Stay exact-match v4, or introduce read-old/write-current migration. The round-trip data loss (§6.4) makes "open old, save, lose everything unknown" the current de facto migration.
12. **OCR text inside the project XML.** Keep (single-file project, simple) vs sidecar (the XML can grow by megabytes of recognized text per book, and the project file becomes content-bearing — relevant to the owner's privacy stance on published artifacts).
13. **Temp-dir-as-output lifecycle (F6).** Hash by project path only vs per-session unique dirs; and should saving a project force migration of outputs out of `$TMPDIR`?
14. **`external_alarm_cmd` → `std::system()`.** Keep (power-user escape hatch, inherited) vs remove/allow-list on a signed, notarized macOS app.
15. **Cancel-before-decode.** (a) move `throwIfCancelled` above `ImageLoader::load` in `LoadFileTask` — one line, saves seconds per cancelled page, no downside found; (b) additionally pass the cancel flag into `PdfReader` for mid-rasterization abort — larger, probably unnecessary. (Recorded as the auditor's options, not a decision.)
16. **DeviationProvider fix shape.** (a) give the provider its own mutex (self-contained, slightly redundant locking); (b) route all reads through locked Settings methods and stop exporting the bare `const&` (cleaner API; touches 8 call sites in 4 filters + 4 order providers); (c) snapshot deviations into the cache-driven task at creation time on the main thread (eliminates the shared read entirely; most work).
17. **Main-thread PDF import scan.** Accept the freeze, or move `readMetadata`/DPI detection onto a worker with a progress dialog (the export path at `MainWindow.cpp:283` is the existing pattern). Relevant to the Zotero hand-off flow where large PDFs arrive unattended.
18. **BUILD.md and create-dmg.sh.** (i) bring both into SIGNING.md compliance; (ii) delete them and let SIGNING.md be the sole release doc; (iii) keep the landmines with their warning label. Currently (iii).
19. **Tracked release archaeology.** Keep `tmp/release-2.0b1-*` (707 tracked files, ~622 MB dir) and the 5 stale root session docs in history forever, or prune. (`dist/` DMG tracking is owner policy; `tmp/` looks accidental.)
20. **The 5 unregistered diagnostic executables.** Promote to CTest, demote behind an EXCLUDE_FROM_ALL option, or delete.
21. **Stage-8 options breadth** is the root cause of two of the four worst files (OptionsWidget 1,795 and OutputGenerator 3,309 lines driven by ~40 interacting options). Is trimming/consolidating user-facing output options on the table (e.g. auto-only modes), or is full manual control a product commitment? The answer bounds how much of that complexity is reducible at all.
22. **Stage-8 output filesystem side effects across subpages** and the second on-disk copy made by save-to-folder are behaviors to confirm as intended when any of the above is revisited (minor; noted for completeness).

*(Owner-stated constraint on record, from project memory: the Force B&W preset is literal by design — it destroys all grays including photos; no photo-safe exception is to be added. Any output-option consolidation under decision 21 must respect this.)*

---

*End of audit. This document records findings only; the staged restructuring proposal is a separate deliverable.*
