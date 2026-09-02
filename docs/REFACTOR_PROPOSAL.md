# ScanTailor Spectre — Staged Refactoring Proposal

> **STATUS: DRAFT — AWAITING OWNER REVIEW.**
> No stage may execute until the owner approves that stage explicitly. Approval of one stage
> approves only that stage. Stage 0 items are individually approvable questions; Stage 1 items are
> individually approvable extractions.

**Date:** 2026-08-26.
**Inputs:** `docs/ARCHITECTURE_AUDIT.md` (the approved audit; cited below as "audit §N") and
`docs/REFACTOR_PREREQS.md` (the render-driver divergence diff and defect verification; cited as
"prereqs"; where prereqs and audit differ, prereqs corrects the audit).
**Test baseline:** all 6 CTest suites green at commit `bb3db78c` (`math_tests`,
`imageproc_tests`, `core_tests`, `zotero_client_tests`, `export_options_layout_tests`,
`output_exposure_controls_tests`; 5.2 s wall). Five diagnostic executables exist but are not
registered with CTest (`color_detection_diagnostic`, `geometry_diagnostic`,
`mixed_output_diagnostic`, `margins_output_diagnostic`, `full_pdf_bw_diagnostic`).

---

## Preliminaries — every stage assumes these

1. `git tag pre-refactor` at the starting commit (`bb3db78c`, the green-baseline commit).
2. All work happens on a branch (`refactor/stage-N-<slug>`), never on `main` directly.
3. **One extraction per commit.** Never batch extractions; bisect must have granularity.
4. The project compiles after every step
   (`cmake --build build --target scantailor -j$(sysctl -n hw.ncpu)`).
5. `ctest` is green (all 6 registered suites) after every step. Any suite added in Stage 1.5
   joins this gate the moment it lands.
6. Project rules stay in force: log to `BUILD_LOG.md` before every build; `build/` is the only
   build location.
7. **Green tests prove the tested layer, not the app** (staged-refactor skill rule). Every
   stage's completion additionally requires the owner to build, launch, and smoke-test the real
   app on **copies** of real projects. No stage is "done" on CI-style evidence alone.

---

## Stage 0 — Decisions with no code

These are the audit's Product Decisions (audit §10) and the two prereqs findings that gate later
stages. Each is a question with options and trade-offs. **None are decided here.** Items marked
"gates:" block the named later work until answered; unmarked items merely unlock dead-weight
deletion in Stage 1.

### 0.1 libharu (audit §10.2)

Found, linked into core, documented, referenced by **zero** source lines (audit §8.3); the live
exporter is `PdfExporter.mm` (CGPDFContext).

- **(a) Remove** the find module, link line (`src/core/CMakeLists.txt:203`), brew-line mention,
  and BUILD.md praise. Cost: none found; the code already voted. Risk: near zero.
- **(b) Implement** HPDF-based compression as once advertised. Cost: a new export backend on top
  of a working one. No evidence anyone wants this.

Gates: Stage 1 item 1.8 (dead-weight deletion).

### 0.2 ImageTypeDetector (audit §10.3)

Compiled into core, zero users, superseded by `LeptonicaDetector`, no tests; actively misleads
navigation (audit §7.2 flags it as a look-alike trap for the color detector).

- **(a) Delete.** Risk near zero (no references).
- **(b) Keep with a header comment** explaining why it is retained. Cost: continued confusion.

Gates: Stage 1 item 1.8.

### 0.3 Metal morphology (audit §10.1)

Hard-disabled (`MetalMorphology.mm:25-29` returns false unconditionally, unreproduced
objc_msgSend crash TODO) yet fully compiled and shader-bundled; docs claim all Metal is off while
Gauss blur is live (audit §1 acceleration).

- **(a) Root-cause and re-enable** (TODO.md's plan; its Phase 1 is already done). Cost: an
  open-ended debugging effort against an unreproduced crash.
- **(b) Delete** `MetalMorphology.*` + its shaders, keep vImage/CPU morphology and live Metal
  Gauss blur. Cost: ~an afternoon; re-adding later means restoring from git.
- **(c) Freeze as-is** (the current de-facto choice). Cost: dead GPU code and shaders built and
  bundled forever, plus permanently wrong docs.

Whichever way: CLAUDE.md/TODO.md must be corrected to match reality (audit §8.5).
Gates: Stage 1 item 1.8 (option b) or nothing (options a/c).

### 0.4 QtWebEngine for one panel (audit §10.4)

core PUBLIC-links WebEngineWidgets/WebChannel solely for the weasel Photo Adjustments panel
(`src/core/CMakeLists.txt:210`); ~200 MB QtWebEngineCore ships in every bundle; `webui.qrc` also
ships an unreferenced `fix_orientation.html` (audit §1 core/weasel).

- **(a) Strategic direction:** web panels are the future of filter UIs; accept the link and
  bundle weight; `fix_orientation.html` is planned work.
- **(b) Experiment over:** rewrite the one panel as native QWidgets, drop WebEngine — the
  heaviest dependency in the app — and delete the stray HTML. Cost: a real UI rewrite of a
  shipped feature; `TonalCurve`/`PhotoAdjustments` math is UI-independent and portable (audit
  §9-Q19), so the model survives.
- **(c) Keep as-is, delete only the stray HTML.**

Gates: the shape of Stage 2B (whether the SCC plan must keep a WebEngine-facing seam in core) and
blast-radius question Q19.

### 0.5 The user-visible "Weasel" string (audit §10.5)

`src/app/AutoProcessDialog.ui:58` ships "…from Weasel's processed page images…" to users.

- **(a) Rename to Spectre** — one-line .ui change + retranslation.
- **(b) Keep** as an easter egg.

Gates: nothing; a Stage 1 candidate only if (a), and note it is a *string change*, i.e. not
zero-behavior — it rides in Stage 1 only with explicit approval as a labeled exception.

### 0.6 Tracked `tmp/` release bundle (audit §10.19)

707 tracked files (~622 MB directory) — a complete extracted 2.0b1 app bundle plus its
`dmg-root/` twin, apparently accidental (audit §8.1). `dist/` DMG tracking is owner policy and is
**not** questioned here.

- **(a) Prune** the `tmp/` tree from the tip (plain `git rm -r --cached` + .gitignore; history
  retained on the private forge per policy).
- **(b) Keep** as a deliberate release archive.

Gates: Stage 1 item 1.8. (Note: any history rewrite is out of scope of this proposal entirely;
option (a) means tip-only removal.)

### 0.7 BUILD.md and create-dmg.sh (audit §10.18)

`packaging/macos/BUILD.md` is wrong end to end (names the product ScanTailor Advanced, omits
Leptonica, praises libharu, recommends the non-compliant script); `create-dmg.sh` uses the wrong
keychain profile and `--deep` signing, and SIGNING.md already declares it non-compliant (audit
§8.1, §8.5).

- **(i) Bring both into SIGNING.md compliance.** Cost: real script work + verification against a
  release.
- **(ii) Delete both;** SIGNING.md becomes the sole release doc. Cost: none found; SIGNING.md is
  already authoritative.
- **(iii) Keep the landmines with their warning label** (current state).

Gates: Stage 1 item 1.8 (option ii).

### 0.8 D1 — `FILL_BLACK` under dewarping (prereqs §1.2 D1)

"Fill margins: black" works without dewarping and is silently ignored with dewarping (accidental
upstream drift, commit `87af4841` patched only one driver). Any driver unification must pick one
behavior; the choice is user-visible in shipped output files.

- **(a) Black everywhere:** port the FILL_BLACK switch into the dewarp path. Changes output for
  existing dewarped projects that had black selected (they silently got white/background before).
- **(b) Freeze the asymmetry** as documented behavior. Cost: the unified driver must carry an
  explicit "ignore FILL_BLACK when dewarping" branch, forever, with a comment.
- **(c) Remove FILL_BLACK** from the UI. Changes the product; probably hostile to existing
  projects that serialize it.

Gates: Stage 2A (driver unification) and the Stage 1.5 golden matrix (which expected image is
"correct" for the dewarp+fill-black cell).

### 0.9 D2 — fix authorization for the null-deref (prereqs §1.2 D2)

Statically airtight null-pointer dereference in the **non-dewarp** driver
(`OutputGenerator.cpp:1558` releases `bwContent`, then 1564/1572 use it when
`originalBackground()`), inherited from upstream `545ef72c`. Not runtime-reproduced; if the
configuration provably works in the shipped app, the finding regrades to dead code (prereqs
could-not-verify #1).

- **(a) Authorize the fix** (mirror the correct with-driver shape: don't release before the
  original-background uses), landed as a Stage 1.5 quick win **after** its characterization test
  exists (see §1.5-QW1). This is a behavior change on paper (crash → output) and is recorded as
  such.
- **(b) Reproduce first:** require the runtime repro (MIXED + split output + B&W foreground +
  original background, no dewarp) before touching the line. Slower; strictly more evidence.

Gates: Stage 1.5 QW1 and Stage 2A (the unified driver must adopt exactly one of the two shapes).

### 0.10 Version wall / compatibility window (audit §10.11, §6.4; prereqs Part 2e)

`ProjectReader` requires `version == 4` exactly in both directions; saves rebuild the document
from memory so anything unknown is dropped; defaults profiles share the version constant, so a
bump invalidates every user profile.

- **(a) Stay exact-match v4.** No migration code; any future format change orphans old projects
  and all profiles. Current de-facto policy.
- **(b) Read-old / write-current migration layer.** Cost: real machinery + migration fixtures;
  benefit: format changes stop being walls. Sub-question the owner must answer either way: **is
  rollback to older builds (new file opened by old build) in the compat window, or only
  forward?**

Gates: Stage 2C (persistence hardening) in its entirety, and the design of Stage 1.5's migration
fixtures (what must they prove).

### 0.11 Temp-dir-as-output lifecycle (audit §10.13; F6; prereqs Part 2d)

`$TMPDIR/scantailor-spectre-<md5(sourcePath)[:8]>` collides across projects from the same PDF, is
serialized into the XML (possibly as a relative path through `/var/folders/…`), is never
recreated on reopen, and is recursively deleted on close.

- **(a) Per-session unique dirs** (add a nonce): kills the cross-project collision; orphans temp
  outputs of a crashed session until OS cleanup.
- **(b) Keep hashing by source path** but never serialize a `$TMPDIR` path into the project (save
  forces choosing/migrating a real output dir).
- **(c) Both** (a)+(b).

Gates: Stage 2C scope, and the F6-related quick wins (none proposed before this is decided,
because any fix changes what saved XML contains).

### 0.12 `external_alarm_cmd` → `std::system()` (audit §10.14)

A hand-editable INI value (`main_window/external_alarm_cmd`, no in-app writer) executed via
`std::system()` at batch completion (`MainWindow.cpp:2585-2591`) — arbitrary command execution
from a config file, inherited from upstream, in a signed, notarized app.

- **(a) Remove** the hook. Cost: any power user relying on it loses it (no evidence of use).
- **(b) Keep** as a documented power-user escape hatch.
- **(c) Constrain** (allow-list / `QProcess` with no shell). Middle cost.

Gates: nothing structural; a Stage 1-adjacent one-liner once decided (deletion) or a small task
(constraint). It is listed in Stage 0 because it is a security posture question, not a code
question.

### Stage 0 exit criterion

The owner has answered, or explicitly deferred, each of 0.1–0.12. A deferred answer removes the
gated items from later stages rather than blocking the stages entirely.

---

## Stage 1 — Mechanical, zero-behavior-change extractions

**Risk class: LOW** (mechanical moves of code with narrow inputs; no logic edits, no signature
redesigns beyond what the move itself forces).
**Rollback story:** every item is one commit on the stage branch; revert the commit. The
`pre-refactor` tag bounds the whole stage.
**Verification gate per item:** compiles, `ctest` green, `git diff --stat` shows only the moved
code plus include/CMake adjustments, and a skim proving the moved bodies are textually identical
(whitespace/namespace aside). Stage-level gate: owner launches the app and exercises the touched
surface (runs a batch to see the summary dialogs, exports a PDF, runs Auto Mode once) on a copy
of a real project. Green ctest proves the tested layer, not the app.
**Blast-radius questions this stage must answer first (by name, from audit §9):** Q5 ("any
consumer of Task/CacheDrivenTask constructors other than MainWindow and the neighbor filter?") —
answer needed only if an extraction touches `createCompositeTask`; the items below deliberately
do **not** touch it, so Q5 is noted, not blocking.

Items ranked by leverage-to-risk (highest first). Each is separately approvable and lands as its
own commit (or short commit series for the multi-file ones).

### 1.1 MainWindow summary/remediation dialog suite → `src/app/BatchSummaries.{h,cpp}` (+ helper)

- **What moves:** ~590 lines / ~12 methods at `MainWindow.cpp:4542-5133`:
  `showBatchProcessingSummary` (80), `showPageBoxSummary` (77), `showContentCoverageSummary`
  (72), `showPageSizeWarning` (181), and the remediation actions (`forceTwoPageForImages`,
  `forceSinglePageForImages`, `preserveLayoutForPages`, `disableAlignmentForPages`, jump-to-page
  ×3) (audit §4.1 section map).
- **Where to:** a new `app/` class taking `StageSequence&`, `ProjectPages&`, and a narrow
  callback interface for "invalidate thumbnails / jump to page" instead of `MainWindow&`.
- **Evidence it's untangled:** audit §4.1 explicitly lists the suite under "mechanically
  extractable (narrow inputs)": each is a self-contained walk over pages reading/writing one
  filter's Settings plus a dialog. It does not participate in the batch state-machine call graph
  (`filterResult` triggers the dialogs but the dialogs do not call back into batch state).
- **Gate:** as stage-level, plus: run a batch on a project engineered to trip each summary and
  confirm each dialog appears and each remediation button still acts.

### 1.2 MainWindow PDF-export flow → `src/app/PdfExportFlow.{h,cpp}`

- **What moves:** `MainWindow.cpp:2755-3041` (~290 lines): `exportToPdf` (116),
  `exportToPdfFromFilter` (170), plus the anon-namespace export-success dialogs and
  `runPdfExportInBackground` (`:283-340`) that only they use (audit §4.1).
- **Where to:** an app-layer flow object around `core/PdfExporter.mm`; inputs are the output
  file list, `OutputFileNameGenerator`, and parent-widget handle.
- **Evidence:** audit §4.1 lists "PDF export (~290 lines of dialog/naming/progress around
  PdfExporter.mm)" as mechanically extractable; the flow's only ties back into MainWindow are
  reads of `m_outFileNameGen`/`m_pages` and the status-bar/progress surface.
- **Caution noted, not fixed:** the nested `QEventLoop` waits (audit §5.3) move verbatim.
  Removing them is *not* part of this stage.
- **Gate:** as stage-level, plus one real export (menu path and export-filter path) diffed
  byte-identical against a pre-move export of the same project.

### 1.3 MainWindow auto-accept helpers → `src/app/AutoAccept.{h,cpp}`

- **What moves:** the auto-accept helpers for page split / deskew / content / page-size inside
  `MainWindow.cpp:2032-2362` (audit §4.1: "each a self-contained walk over pages writing one
  filter's Settings").
- **Where to:** free functions or a small struct taking `StageSequence&` + `ProjectPages&`.
- **Evidence:** same audit line; they are called from the auto-mode machine but do not read
  auto-mode state themselves.
- **Explicitly NOT moved:** `autoModeAdvance`, `finishAutoProcess`, the stage timers, and
  anything touching `m_autoModeStage` — that is the entangled state machine (audit §4.1
  "extraction would be surgery"), deferred to Stage 2D.
- **Gate:** as stage-level, plus one full Auto Mode run on a small copy project comparing the
  resulting per-filter Settings XML (save → diff) against a pre-move run.

### 1.4 Benchmark/env-flag CSV instrumentation → `src/app/BenchmarkLog.{h,cpp}`

- **What moves:** the anon-namespace benchmark env-flags + CSV writer (`MainWindow.cpp:31-208`
  portion) and the CSV-writing block inside `finishAutoProcess` (audit §4.1 section map; §10.8).
- **Where to:** a small app-layer recorder object; `finishAutoProcess` keeps a two-line call.
- **Evidence:** env-gated, write-only, no reads back into window state.
- **Note:** audit §10.8 asks whether this is developer tooling to strip or a supported feature —
  extraction is compatible with either answer, so this does **not** wait on Stage 0.
- **Gate:** as stage-level; run once with the benchmark env var set, confirm identical CSV
  columns.

### 1.5 Temp-output cleanup block → `src/app/TempOutputCleanup.{h,cpp}` *(optional, flagged)*

- **What moves:** `MainWindow.cpp:3641-3717` (`cleanupTempOutputFiles`,
  `showTempCleanupWarning`, the prefix guard) (audit §4.1).
- **Why flagged:** this code performs `QDir::removeRecursively()` — permanent deletion (audit
  §6.6). The move is mechanical, but any transcription slip is destructive. Recommend landing it
  *after* 1.1–1.4 have demonstrated clean process, with a line-by-line review of the guard
  conditions, or skipping it entirely until Stage 0.11 decides the temp-dir lifecycle (which may
  rewrite this code anyway).
- **Gate:** as stage-level, plus a manual test that the warning dialog still appears and Cancel
  still cancels, on a throwaway project.

### 1.6 OutputGenerator anonymous-namespace helper library → `src/core/filters/output/OutputGeneratorUtils.{h,cpp}`

- **What moves:** the ~900-line anon-namespace helper library at the top of
  `OutputGenerator.cpp` — fill-zone appliers (both the `QTransform` and
  `std::function<QPointF(QPointF)>` overloads, `OutputGenerator.cpp:749-855`), margin filling,
  `findRectAreas` (174 lines), affine transform helpers, hit-miss ops,
  `reserveBlackAndWhite` (audit §4.2).
- **Where to:** a same-directory internal header/impl pair (not exported outside the output
  filter); functions become `output::detail` or stay in an unnamed-namespace-per-TU replacement
  with internal linkage preserved via the new TU.
- **Evidence it's untangled:** audit §4.2: "the anon-ns helpers … have narrow inputs"; prereqs
  §1.1 step 6 confirms the fill-zone helpers are the cleanest existing seam in the file (already
  parameterized over transform vs mapper). None touch `Processor` members.
- **What does NOT move:** anything inside `Processor` (35 members, 34 methods) — audit §4.2 is
  explicit that the Processor is not mechanically splittable before the pipeline-contract
  decision; that is Stage 2A's problem.
- **Gate:** compiles + ctest green + (once Stage 1.5 goldens exist, re-run them; if this item
  lands before the goldens, the gate is the diagnostic executables run by hand:
  `mixed_output_diagnostic`, `margins_output_diagnostic`, `full_pdf_bw_diagnostic` produce
  unchanged output). Owner smoke-test: re-run Output on one real page, diff the TIFF.

### 1.7 `settings/batch_processing_threads` key consolidation (F5)

- **What moves:** the duplicated literal in `SystemLoadWidget.cpp:12,48-50` and
  `WorkerThreadPool.cpp:111` into one `ApplicationSettings` accessor pair (audit §3.2 F5).
- **Evidence:** same string, same semantics, two files; consolidation is textual. The
  `remove()`-at-max behavior moves verbatim.
- **Gate:** ctest green + manual check that the threads spin-box still round-trips (set, restart
  app, verify).

### 1.8 Dead-weight deletions — **gated on Stage 0 answers**

Each lands only if the corresponding Stage 0 item chose deletion, one commit each:

| Item | Stage 0 gate | What is deleted |
|---|---|---|
| libharu find/link/docs | 0.1(a) | `cmake/FindLibHaru.cmake`, link line, doc mentions |
| ImageTypeDetector | 0.2(a) | the class + CMake entry |
| Metal morphology | 0.3(b) | `MetalMorphology.*`, its shaders, metallib references |
| Tracked `tmp/` bundle | 0.6(a) | `git rm -r --cached tmp/…` + .gitignore line |
| BUILD.md / create-dmg.sh | 0.7(ii) | both files; SIGNING.md gains a one-line note |
| `fix_orientation.html` | 0.4(b or c) | the qrc entry + file |
| dead `cache/predespeckle/` helper, dead `preserveOutput` state, `OpenGLSupport.cpp` dead include | none (audit-verified zero callers: §3.2 F2, §3.4, §8.1) | the dead members/functions |

- **Gate per deletion:** full build + ctest + `grep` proving zero remaining references; for the
  `preserveOutput` removal, re-read `cleanupTempOutputFiles` guards afterward (the always-false
  guard becomes no guard — behavior identical, but verify by reading, since this is the
  destructive-delete path).

### Explicitly EXCLUDED from Stage 1

- **ImageViewBase** (1,074 lines, 44 members): audit §4.6 flags it "high blast radius, low
  fork-specific churn: flag, don't touch", and audit §9-Q9 records **zero** characterization
  coverage of its zoom/pan/HQ-rebuild behavior. It is the base of all 10 filter ImageViews. No
  extraction, no cleanup, no "quick renames" in this file at any stage of this proposal.
- The batch/auto-mode state machine, `filterResult`, `createCompositeTask` — entangled (audit
  §4.1); Stage 2D.
- `DefaultParamsDialog` de-duplication — behavioral/structural (audit §10.6); Stage 2+.
- `ThumbnailPixmapCache`, `ContentBoxFinder` — sealed components (audit §4.6).

---

## Stage 1.5 — Characterization tests (pin behavior before anything behavioral)

**Risk class: LOW for the tests themselves** (test-only commits; app code untouched except where
a seam-shim is unavoidable, and any such shim is its own reviewed commit). **The quick wins at
the end of this stage are behavior changes** and carry their own gates.
**Rollback story:** test commits are additive; revert freely. Quick-win commits revert
individually.
**Blast-radius questions answered here (audit §9):** Q8 (driver divergence — answered by prereqs
Part 1 + the goldens making it executable), Q10/Q11 (persistence tolerance and version wall —
made executable by the fixtures), Q15 (deviation race consequences — bounded by QW2's test).

### 1.5.1 Golden-image characterization for OutputGenerator

The prerequisite for Stage 2A. Pins what both drivers render **today**, including the D1/D3
asymmetries, before anyone touches them.

- **Fixture strategy (inputs):** two small synthetic source pages committed to the repo
  (~600×800 px, deterministic, generated once by a fixture-writer program whose source is also
  committed): (i) a "text page" — black strokes on off-white with a slight tint and a gradient
  (exercises binarization, illumination normalization, despeckle); (ii) a "mixed page" — the
  text page plus an embedded continuous-tone photo block and a dark frame (exercises picture
  detection, picture shapes, fill zones over both content kinds). Committed as PNGs; tests copy
  them to a temp dir before use (fixture bytes never mutate — skill rule).
- **Matrix (cases, not full cross-product):** both drivers exercised through
  `OutputGenerator::process` exactly as `output::Task` calls it. The six `ColorMode` values
  (`ColorParams.h:21`: BLACK_AND_WHITE, COLOR_GRAYSCALE, MIXED, COLOR, GRAYSCALE, AUTO_DETECT) —
  AUTO_DETECT resolves at finalize time, so the render matrix covers the five concrete modes,
  and one case documents that AUTO_DETECT never reaches the render layer. Crossed selectively
  with: dewarp off / dewarp **MANUAL with a fixed committed distortion model** (deterministic;
  AUTO model building is content-dependent and is pinned separately by one looser case) / fill
  zones present + absent / `FillingColor` background–white–**black** (the D1 cells: fill-black ×
  dewarp on and off, both pinned as-is) / picture shape options (free, rectangular) on the mixed
  page / splitting + original-background on the mixed page (the D2 configuration — see QW1).
  Target ~30 golden cases.
- **Comparison:** byte-exact against committed golden PNGs, with the machine caveat recorded:
  Metal Gauss blur is foreground-gated and almost certainly off in test binaries (audit §1
  acceleration, "unverified"), so goldens are CPU-path goldens; the test asserts the CPU path
  explicitly (or records which path ran) so a future Metal change fails loudly rather than
  flaking.
- **Harness:** a new Boost.Test target (`output_golden_tests`) with offscreen QPA (pattern
  already exists: `export_options_layout_tests`, audit §8.2), registered with CTest and added to
  the standing green gate. First run generates goldens into the repo under review; the owner
  eyeballs each golden once before it is committed as truth.

### 1.5.2 Project save→load→save XML equality test

- Builds a project in memory with **all 10 filters populated** with non-default per-page params,
  saves, loads, saves again, and asserts the two serializations are equivalent (canonicalized
  DOM compare). This closes the audit's biggest untested persistence surface: the 10 filter XML
  dialects (audit §3.3 — `TestProjectPortability` currently writes an *empty* filter vector).
- Requires QApplication (Filter ctors construct OptionsWidgets — audit §3.3); use the offscreen
  QPA pattern. If a filter proves unconstructible offscreen, the fallback seam (splitting
  settings-serialization from widget construction) is a Stage 2 change, not something to smuggle
  in here — in that case the test covers the filters that do construct, and the gap is recorded.

### 1.5.3 Migration fixtures — frozen v4 project files

- Freeze 2–3 real v4 `.ScanTailor` files (small, personal-data-free — synthetic source images,
  since project XML can embed OCR text, audit §10.12) as committed fixtures, **captured from
  disk before any current build has opened them** (prereqs implication: open→save is lossy by
  design today; F1 can rewrite metadata on open).
- Tests copy each fixture to a temp dir, open via `ProjectReader`, and assert: open succeeds,
  page counts and key per-page params match a committed expectation file. Fixture bytes never
  mutate.
- One fixture is a **600-DPI PDF-import project** — the F1 case. Its test initially *documents*
  current behavior (reopen renders at 300; `updateImageSizeIfChanged` would rewrite metadata)
  and becomes the regression test for QW5.

### 1.5.4 Decision point: the five diagnostic executables (audit §10.20)

Presented for owner decision alongside this stage, since the golden harness overlaps them:

- **(a) Promote** to CTest (cheap smoke value; they already build on every full build).
- **(b) Demote** behind `EXCLUDE_FROM_ALL`.
- **(c) Delete** those the golden suite supersedes (`mixed_output_diagnostic`,
  `margins_output_diagnostic`, `full_pdf_bw_diagnostic` overlap 1.5.1's territory), keep or
  promote the rest.

### Quick wins (gated) — defect fixes that may land EARLY, each its own approvable item

Skill rule applied: isolated user-visible defect fixes may land before Stage 2, **but only after
the characterization test that covers each one exists and is green against current behavior**
(or, for a crash, is written and demonstrably fails/crashes). Each item = one commit, one
approval.

| # | Fix | Defect source | Gating test (must exist first) | Notes |
|---|---|---|---|---|
| QW1 | D2 null-deref: stop releasing `bwContent` before the original-background uses (`OutputGenerator.cpp:1558` vs 1564/1572); mirror the correct with-driver shape | prereqs §1.2 D2 | The 1.5.1 golden case for MIXED + split + B&W-foreground + original-background, non-dewarp — which today should crash the harness (confirming D2) or pass (regrading D2 to dead code per prereqs could-not-verify #1) | Requires Stage 0.9(a). Recorded as a behavior change (crash → output) |
| QW2 | DeviationProvider race: add locking per audit §10.16 — recommended shape (a) provider-internal mutex as the minimal fix; shape (b)/(c) are Stage 2 API work | audit §5.5-1 as corrected by prereqs Part 2(a) | A unit test pinning `DeviationProvider` semantics (mean/stddev/isDeviant for a known key set, incl. recompute-after-update), so the locked version provably computes identical statistics; plus one TSan run of `core_tests` before/after showing the report count drop | Fix shape (a) is self-contained: one header, no call-site changes |
| QW3 | Cancel-before-decode: move the first `throwIfCancelled()` above `ImageLoader::load` in `LoadFileTask::operator()` (`LoadFileTask.cpp:61-64`) | audit §5.2, §10.15(a) | A `core_tests` case constructing a `LoadFileTask` with an already-cancelled status and asserting no decode occurs (observable via `ImageLoader` stats counters or a sentinel path that would fail loudly if decoded) | One line + test; audit records "no downside found" |
| QW4 | TIFF-compression no-op (F2): implement whichever wiring Stage 0 / audit §10.9 chose; includes the enum translation (finalize values 0/1 vs libtiff 4/5/8 — prereqs Part 2c) and deleting the false comment at `output/Task.cpp:64-66` | audit F2; prereqs Part 2(c) | A test pinning `TiffWriter`'s compression selection from the `settings/bw_compression`/`color_compression` keys (current behavior), extended to cover the new sync path | **Gated on Stage 0 answer to audit §10.9** — the fix direction is a product decision |
| QW5 | PDF import DPI (F1): serialize per-source render DPI (or re-derive from `ImageMetadata`) per Stage 0 / audit §10.10, **and** guard `updateImageSizeIfChanged` against the reopen-at-wrong-DPI case so stored geometry is never silently rewritten | audit F1 as upgraded by prereqs Part 2(b) | The 1.5.3 600-DPI fixture test (documents current lossy behavior first, then asserts the fix: reopen renders at 600, metadata unchanged, save→diff clean) | **Gated on Stage 0 answer to audit §10.10.** The metadata-rewrite guard is the critical half — without it, one reopen on an unfixed build still corrupts the file permanently |

**Stage 1.5 verification gate:** all new suites green in CTest; goldens reviewed by eye once;
fixtures committed with a README naming their provenance; each quick win individually approved,
landed, and its gating test flipped from "documents old behavior" to "asserts new behavior" in
the same commit as the fix. Then the stage-level owner smoke test: open a real project copy,
run a batch, export — because green tests prove the tested layer, not the app.

---

## Stage 2+ — Behavioral / structural work

Every Stage 2 item is gated on Stage 1.5 being complete and on the named prerequisite documents
and decisions. These are sketches to be turned into per-stage plans at approval time — each will
get its own short plan document before execution.

### Stage 2A — Render-driver unification

- **What:** merge `processWithoutDewarping` / `processWithDewarping` over an explicit coordinate
  context ("working CS + affine lift" vs "output CS + mapper" — prereqs §1.2 closing paragraph),
  so fork features stop paying the two-copy tax (prereqs §1.1 step 7).
- **Gates:** `REFACTOR_PREREQS.md` Part 1 (the divergence table — exists); the 1.5.1 golden
  matrix (green); owner decisions 0.8 (D1) and 0.9 (D2); QW1 landed or D2 regraded.
- **Risk class: HIGH.** The drivers are not equivalent copies (D1, D2, D3); D5 means the unified
  driver's *persistent side-effect set* (Settings writes at `OutputGenerator.cpp:1783`,
  `distortionModel` out-param) must be reproduced exactly per-mode or staleness comparison
  (`OutputImageParams`) changes when pages regenerate.
- **Blast-radius questions it must answer first:** audit §9-Q8 (answered by prereqs Part 1 —
  every divergence classified); plus one new: which helper seams (`transformToWorkingCs`,
  `processPictureZones`) change signature, and does anything outside OutputGenerator.cpp see
  them (prereqs §1.3 says no — verify at plan time).
- **Rollback:** the goldens make regression detection mechanical; the stage is one branch,
  revertible wholesale; D3 (wiener ordering) is resolved *explicitly in the plan* — whichever
  ordering is chosen, the affected golden cells are regenerated in a commit that says so.
- **Verification gate:** goldens byte-identical for every cell the plan declares unchanged;
  changed cells enumerated with owner sign-off; diagnostics (if kept) unchanged; owner re-runs
  Output on a real dewarped book copy and visually compares.

### Stage 2B — core ↔ filters SCC untangling

- **What:** break the 13-directory strongly connected component (audit §2.2: 20 core→filters
  includes vs ~520 back; core→zones→interaction→core), starting where the audit points: hoist
  the six option types out of `core/DefaultParams.h:6-14`, invert `StageSequence.h:12-21`, and
  decide the true home of the de-facto shared models `output/Settings.h` (26 includers) and
  `output/ColorParams.h` (19).
- **Gates:** a **dependency-target plan** document (to be written at approval time) that answers
  audit §9-Q1–Q4 by name: Q1 what consumes `DefaultParams` serialization and can the six types
  move without changing persisted XML; Q2 which parts of output/Settings are persisted state vs
  runtime; Q3 how much of core's PUBLIC surface filters actually use; Q4 whether the
  pipeline-tail direction flip is an ordering artifact or a real data dependency. Also gated on
  Stage 0.4 (whether core must keep a WebEngine seam).
- **Risk class: MEDIUM** (moves types with persisted representations; XML schema must be proven
  byte-stable by the 1.5.2 round-trip test and 1.5.3 fixtures).
- **Rollback:** per-move commits; the round-trip test failing is the tripwire.
- **Verification gate:** include-graph re-scan shows the SCC broken as planned; CMake gains the
  previously-missing inter-filter link declarations (audit §2.6) so regressions become
  configure-time errors; ctest + fixtures green; owner smoke test.

### Stage 2C — Persistence hardening

- **What:** whatever Stage 0.10 chose: unknown-element preservation and/or a versioned migration
  layer in `ProjectReader/Writer`; decoupling the defaults-profile version from the project
  version; making malformed-per-page handling non-destructive (quarantine instead of silent
  default + erase — audit §6.4, prereqs Part 2e); F6 lifecycle changes per Stage 0.11.
- **Gates:** Stage 0.10 (compat window — the whole stage's shape depends on it), Stage 0.11,
  1.5.2 round-trip test, 1.5.3 fixtures (including post-migration-bytes tests: any migration
  test must open the *migrated* output, not just the frozen input — skill rule).
- **Risk class: HIGH** (migrations are irreversible on real data). The owner's smoke test here
  is the skill's canonical one: open a **copy** of a real, old, heavily-edited project on the
  new build, diff its save against the same project saved by the pre-refactor build, and review
  every difference. Green fixtures do not stand in for this.
- **Rollback:** for code, git; for data, none — which is why everything runs copy-first and the
  fixtures gate is absolute.

### Stage 2D — MainWindow decomposition beyond the mechanical extractions

- **What:** extract the batch + auto-mode state machine (`startBatchProcessing`,
  `startAutoMode`, `autoModeAdvance`, `filterResult`, `stopBatchProcessing` and their 8+ shared
  members) into a controller; separate `FilterUiInterface` from the window; possibly the task
  factory (`createCompositeTask`).
- **Gates:** Stage 1 complete (MainWindow shrunk by ~1,200 mechanical lines first); answers to
  audit §9-Q5 (other Task-constructor consumers), Q6 (who owns `FilterResultPtr → updateUI`;
  all 10 filters' UiUpdaters assume MainWindow-side effects), Q7 (what
  `stopBatchProcessing(…, resetAutoMode=false)` guarantees — per session memory already a
  re-entrancy trap). Since **no app-layer tests exist at all** (audit §8.2), this stage also
  requires either (a) a minimal headless harness for the batch state machine (queue in, state
  transitions out) written first, or (b) the owner explicitly accepting manual verification as
  the only gate — stated up front, not discovered later.
- **Risk class: HIGH** (the knot; 12+ members read/written by `filterResult` alone, audit §4.1).
- **Rollback:** per-commit; behavioral tripwires are weak here (little coverage), which is why
  this stage is last.
- **Verification gate:** owner runs the full real workflow — import, Auto Mode end-to-end,
  manual batch, cancel mid-batch, quit during batch — on a copy project, against a written
  checklist derived from Q6/Q7 answers.

### Ordering note

2A, 2B, 2C are largely independent of each other (prereqs "implications" section: the deviation
race is independent; TIFF is self-contained; persistence defects interact with each other but
not with the drivers). 2D depends on Stage 1 but not on 2A–2C. Proposed default order:
**2A → 2C → 2B → 2D** (goldens are freshest right after 1.5; persistence hardening protects real
data soonest; SCC work is enabling, not urgent; MainWindow surgery last, with the most caution
and the least coverage). The owner may reorder at approval time.

---

## What this proposal does NOT cover

- **ImageViewBase** — explicitly frozen (audit §4.6, §9-Q9). Any future work there needs its own
  characterization plan first.
- Performance work (the perf-slice branches and Metal re-enablement investigations) — separate
  track; only the Stage 0.3 keep/delete decision touches it.
- Upstream rebase or any relationship with scantailor-advanced master (prereqs could-not-verify
  #7 notes D1/D2 upstream status is unchecked).
- UI/product redesign, output-option consolidation (audit §10.21) — a product conversation, not
  a refactor stage; noted only as the root cause of stage-8 size.
- `DefaultParamsDialog` hand-mirroring (audit §10.6) — depends on the §10.21 conversation; not
  scheduled.
- Documentation corrections (audit §8.5's table: CLAUDE.md errors, README off-by-one, stale root
  session files) — worth doing, mechanical, but doc-only; can ride alongside any stage without
  approval machinery beyond normal review.
- The zotero-plugin JS tests not wired into CTest (audit §1), packaging-script compliance work
  beyond decision 0.7, and history rewriting of any kind.
- OCR-text-in-XML privacy question (audit §10.12) — surfaced to the owner, no stage attached.

## Next action

The owner reviews three documents — `docs/ARCHITECTURE_AUDIT.md`, `docs/REFACTOR_PREREQS.md`,
and this proposal — and **approves or amends Stage 0 and Stage 1 items individually**. Nothing
executes before that. A reasonable first session is: answer the twelve Stage 0 questions (most
are one-word answers), then green-light Stage 1 items 1.1–1.4 as a batch of four independently
committed extractions.
