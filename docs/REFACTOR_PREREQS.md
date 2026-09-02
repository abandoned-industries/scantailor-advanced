# Refactor Prerequisites: Render-Driver Divergence Diff and Defect Verification

**Date:** 2026-08-26 (prepared alongside `docs/REFACTOR_PROPOSAL.md`).
**Tree:** `<repo root>`, branch `main`, at the state audited by
`docs/ARCHITECTURE_AUDIT.md` (source tree identical to baseline commit `bb3db78c`). All line
numbers below are from the current files, re-verified by direct reads, not copied from the audit.
**Method:** read-only. Both OutputGenerator render drivers were read line-by-line in full; every
Part 2 claim was traced through the actual call chain. Git archaeology (`git log -L`, `-S`) was
used to date divergences. No code was changed and no fixes are proposed here.

**Relationship to the audit:** this document is a prerequisite gate for the staged proposal. It
supplements `docs/ARCHITECTURE_AUDIT.md` — and **where the two differ, this document corrects the
audit**, because its claims were traced at trace level rather than survey level. The corrections:

1. **DeviationProvider race worst case (audit §5.5-1, §9-Q15):** the GUI side never writes the
   provider's map; the realistic worst case is a **GUI-thread use-after-free read / crash** during
   an `unordered_map` rehash (plus stale/garbage statistics), **not** heap corruption of the
   Settings object. The race itself is confirmed exactly as the audit states. See Part 2(a).
2. **PDF import DPI (audit F1) is upgraded, not just confirmed:** reopening a project imported at
   non-default DPI silently **rewrites the stored `ImageMetadata` to the 300-DPI size** via
   `LoadFileTask::updateImageSizeIfChanged`, so the **next save permanently corrupts all saved
   geometry** (page boxes, content boxes, zones, distortion models misalign by the DPI ratio).
   See Part 2(b).
3. **New defect D2, not in the audit:** a statically airtight null-pointer dereference in the
   non-dewarp driver (`OutputGenerator.cpp` 1558 → 1564/1572), inherited from upstream commit
   `545ef72c`. See §1.2 D2.

---

## PART 1 — Divergence diff of `processWithoutDewarping` vs `processWithDewarping`

File: `src/core/filters/output/OutputGenerator.cpp` (3,309 lines).

- `OutputGenerator::Processor` declared at **102–263** (35 data members listed 221–262, 34 methods).
- Dispatch: `process` (304) → `processImpl` (1364–1384) computes `m_outsideBackgroundColor` (1374)
  and branches at 1377–1383: dewarp driver when mode is AUTO, MARGINAL, or MANUAL-with-valid-model;
  otherwise the plain driver.
- `processWithoutDewarping`: **1386–1645** (260 lines). `processWithDewarping`: **1647–2010**
  (364 lines).

A fork-wide fact that reshapes both drivers: `RenderParams` **never sets
`NORMALIZE_ILLUMINATION_COLOR` anymore** ("Color illumination normalization replaced by photo
adjustments", `RenderParams.cpp:46-51,58-59`). Every `!m_renderParams.normalizeIlluminationColor()`
guard in both drivers (1534, 1920) is therefore **always true** — the "re-fetch the un-normalized
original" sub-step always runs in mixed+binarization mode. Symmetric in both drivers, so not a
divergence, but any unification must know this flag is dead.

### 1.1 Step-by-step side-by-side

Legend for "Textual": **IDENT** = textually identical up to variable names; **PARAM** = trivially
parameterized (same logic, different coordinate frame/overload/transform argument); **DIV** =
genuinely divergent logic.

| # | Pipeline step | processWithoutDewarping | processWithDewarping | Textual | Verdict |
|---|---|---|---|---|---|
| 1 | Working image / illumination normalization | One image: `maybeNormalized = transformToWorkingCs(normalizeIllumination())` (1394; helper 3078–3101 — normalizes gray, re-brightens color via `adjustBrightnessGrayscale`) | Two images: `warpedGrayOutput` (gray, working CS; 1658–1665, normalized via `normalizeWorkingIlluminationGray` when requested) **plus** `normalizedOriginal` (full original CS; 1671–1691, gray normalization mapped back through `workingToOrig` and applied to the color original) | DIV | **INTENTIONAL.** Dewarping must sample the *original-resolution, original-CS* image after the distortion model is applied; the warped gray twin exists solely for model building, picture detection, and threshold estimation (comment 1655–1657). Cannot collapse to one image without redesigning the dewarp sampling contract. |
| 2 | Wiener denoise | 1399–1402, applied to `maybeNormalized` (cropped working CS), **before** the bg-color recompute at 1404–1407 | 1695–1698, applied to `normalizedOriginal` (full original frame), **after** the bg-color recompute at 1689–1690 and after the 1693 cancel check | PARAM in code, DIV in effect | **ACCIDENTAL (ordering), INTENTIONAL (coordinate space).** Both call sites were added by the same commit (`ab6018d9` "1.0.19: colors: wiener denoiser") with different relative placement: non-dewarp measures background color from *denoised* pixels, dewarp from *raw* pixels. Also full-frame vs cropped cost difference. No evidence the ordering was chosen deliberately. |
| 3 | Outside-background-color recompute (normalize-illumination case) | 1404–1407 from `maybeNormalized` over `m_outCropAreaInWorkingCs` | 1689–1690 from `normalizedOriginal` over `m_outCropAreaInOriginalCs` | PARAM | INTENTIONAL (frame-appropriate); the pre/post-wiener asymmetry is step 2's problem. |
| 4 | BW content binarization (SavGol smooth → binarize → morphological smooth) | Done **up front** for `binaryOutput() \|\| mixedOutput()` (1411–1435): `binarize(maybeSmoothed, m_contentAreaInWorkingCs)` — polygon overload (1423) | Done **twice**, differently: (a) warped-CS binarize inside the mixed branch only (1724–1748), used *solely* to refine the picture mask, then released (1751); (b) the real content binarize happens **post-dewarp** — binary path 1804–1832 `binarize(dewarpedAndMaybeSmoothed, dewarpingContentAreaMask)` (mask overload, 1822), mixed path 1888–1912 with `dewarpedBwMaskFilled` (1900–1902) | DIV | **INTENTIONAL.** Final binarization must run on dewarped pixels; a content *polygon* cannot be pushed through the nonlinear dewarp, so the mask overload is required. The double-binarize (warped for mask refinement, dewarped for content) is a real algorithmic difference a naive unifier would destroy. |
| 5 | Despeckle | Binary path: `maybeDespeckleInPlace(dst, m_outRect, m_outRect, …)` (1452). Mixed path: `(bwContent, m_workingBoundingRect, m_croppedContentRect, …)` (1531–1532) | Binary path: `(dewarpedBwContent, m_outRect, m_outRect, …)` (1838). Mixed path: `(dewarpedBwContent, m_outRect, m_croppedContentRect, …)` (1918) | PARAM | INTENTIONAL: identical policy ("despeckle last before output reconstruction" — comment repeated verbatim 1448–1451, 1527–1530, 1834–1837, 1914–1917), rect args differ only because the non-dewarp mixed image still lives in working CS while the dewarped one is already output-sized. |
| 6 | Fill zones | Affine: `applyFillZonesInPlace(…, m_xform.transform())` (1459, 1485, 1617–1621); mask variant 1622–1625 | Through the dewarping mapper: `origToOutput` std::function built from `DewarpingPointMapper` (1791–1794), used at 1845, 1861, 1982–1986; mask variant 1987–1990 | PARAM | INTENTIONAL and *already parameterized*: the anon-ns helpers (749–855) have overloads taking either a `QTransform` or a `std::function<QPointF(QPointF)>`. The cleanest existing seam in the pair. |
| 7 | Picture zones / auto mask (mixed) | `bwMask` BLACK-init; `processPictureZones(bwMask, pictureZones, GrayImage(maybeNormalized))`; `fillPictureRegionHoles`; `fillPictureFrames`; dbg; `autoPictureMask` export via rasterOp (1499–1517) | Same sequence on `warpedBwMask` with `warpedGrayOutput` as source (1704–1722) | IDENT | Non-divergent. Note the fork's additions (`fillPictureRegionHoles` for `m_continuousToneRegions`, `fillPictureFrames` for `m_pictureFrames`) were kept in sync in **both** drivers — proof the two-copy tax is being paid on every fork feature. `autoPictureMask` is exported in *pre-dewarp* coordinates in both (inherited upstream behavior). |
| 8 | Mask refinement + margin fill | `modifyBinarizationMask` then immediately `fillMarginsInPlace(bwMask, m_contentAreaInWorkingCs, BLACK)` (1519–1520) | `modifyBinarizationMask` on the warped mask (1750), margin fill deferred until after the mask itself is dewarped: `fillMarginsInPlace(dewarpedBwMask, dewarpingContentAreaMask, BLACK)` (1882) | PARAM/DIV | INTENTIONAL: the content area is only expressible as a polygon pre-dewarp and as a rendered mask (`dewarpingContentAreaMask`, built 1796–1802 by dewarping a filled rectangle) post-dewarp. |
| 9 | Dewarp-only block | — | Model build AUTO/MARGINAL (1758–1762); dewarp with trivial-model fallback on `std::runtime_error` (1765–1781); **post-deskew**: `findSkew(dewarped)` → `rotateXform`, `setPostDeskewAngle` (1771–1773, `.0` in fallback 1777); `applyAffineTransform` of every subsequent artifact (1784, 1802, 1881, 1934); **`m_settings->setDewarpingOptions(m_pageId, m_dewarpingOptions)` (1783)** — a persisted Settings write from inside the render driver; mixed-mode second dewarp of the raw original (1920–1935, always taken — dead flag, see preamble) | DIV (whole block absent from the other driver) | INTENTIONAL by definition, but two coupling hazards for any refactor: the Settings write at 1783 (render has a persistent side effect the plain driver lacks) and the `distortionModel` **out-parameter mutation** (1759, 1761, 1776) that the caller (`output::Task`) persists. |
| 10 | Binary-only output assembly (incl. color segmentation + posterize) | 1438–1494: rasterOp working→target, despeckle, invert-if-`!m_blackOnWhite`, fill zones; segmentation branch 1462–1492 (`segmentImage` → optional `posterizeImage` → invert → fill zones) | 1804–1870: same shape on `dewarpedBwContent`/`dewarped`, fill zones via mapper; segmentation branch 1848–1868 | PARAM | Non-divergent in policy; a textbook candidate for the "same steps, different frame" observation. |
| 11 | Mixed combine + reserve B&W + original-background bookkeeping | 1526–1577. **Contains the release-before-use defect — see D2 below**: `bwContent.release()` at **1558**, then `bwContent.inverted()` at **1564** and `rasterOp` from `bwContent` at **1572** when `originalBackground()` | 1888–1967: identical combine/segment/reserve shape, but `dewarpedBwContent` is *not* released before its uses at 1962 and 1988–1989 | DIV | **ACCIDENTAL** — the with-driver is the correct one. Detail in D2. |
| 12 | Filling-color selection | 1592–1604: `needBinarization && !originalBackground` → `switch (getFillingColor())` **honoring `FILL_BLACK`** (1594–1595) else white; else-if `FILL_WHITE` → white/black by `m_blackOnWhite` (1600–1601); **else-if `FILL_BLACK` → black/white by `m_blackOnWhite` (1602–1603)** | 1970–1974: `needBinarization && !originalBackground` → **unconditionally white** (1971); else-if `FILL_WHITE` only (1972–1973); **no `FILL_BLACK` case at all** | DIV | **ACCIDENTAL** — see D1. "Fill margins: black" is silently ignored whenever dewarping is on. |
| 13 | Final assembly (fill margins, drawOver/invert, mixed fill zones, posterize, split masks) | 1606–1644: fill margins on working image, `drawOver` into target-size `dst`, invert, `applyFillZonesToMixedInPlace`/plain, original-background mask AND (1622–1625), posterize-if-not-binarized (1631–1633), split masks lifted by rasterOp (1570–1577, 1635–1643) | 1975–2009: same shape directly on `dewarped` (already target-size), mapper-based fill zones, masks used directly (2000–2008) | PARAM | Non-divergent in policy. The without-driver additionally guards `throw std::bad_alloc()` on a null `dst` (1587–1590) — no equivalent in the with-driver (its `dewarped` came from `RasterDewarper`, different failure mode). |
| 14 | Status/progress & cancellation | **10** `throwIfCancelled` sites: 1409, 1420, 1428, 1432, 1489, 1508, 1517, 1524, 1568, 1629 (+1 inside `transformToWorkingCs` at 3100) | **18** sites: 1693, 1713, 1722, 1734, 1742, 1746, 1755, 1789, 1814, 1827, 1831, 1865, 1886, 1897, 1908, 1935, 1966, 1994. **One asymmetric gap:** the mixed-path `morphologicalSmoothInPlace` at 1910–1912 has **no** following checkpoint, unlike its three siblings (1430–1433, 1744–1747, 1829–1832) | DIV (minor) | ACCIDENTAL (the 1911 gap); the rest of the density difference is INTENTIONAL (the dewarp driver simply has more expensive stages). Neither driver reports progress; `m_status` is cancellation-only. |
| 15 | Debug-image emission | 8 `m_dbg->add` sites, labels: maybeNormalized, smoothed, binarized_and_cropped, bwMask, "bwMask with zones", combined, fillZones, segmented_with_fill_zones | 13 sites; same labels plus norm_illum_color (1685), warpedBwMask variants, dewarped (1787), dewarpedBwContent ×2, dewarpedBwMask | PARAM | Non-divergent in intent; label sets would need reconciling in a unification. |
| 16 | Housekeeping | `maybeSmoothed`/`maybeNormalized`/`bwContent` cleared aggressively (1424, 1441, 1446, 1471, 1558, 1610) | Same pattern, plus a **duplicated dead store** `normalizedOriginal = QImage();` at both 1782 *and* 1785 | IDENT/DIV (trivial) | The 1785 duplicate is harmless drift; the 1558 release is D2. |

### 1.2 The genuine divergences, ranked by risk-if-unified

Risk-if-unified = how likely a naive "merge the two drivers" is to change observable behavior or
destroy a load-bearing difference.

**D1 — `FILL_BLACK` exists only in the non-dewarp driver (HIGH).**
`OutputGenerator.cpp:1592-1604` vs `1970-1974`. Commit **`87af4841`** ("Add option to use black as
filling background color", 2022-02-05, Virgil Grigoras) patched *only* `processWithoutDewarping`
(verified: the commit's diff touches one site in this file). Consequence today: selecting Fill
Margins = black produces black margins without dewarping and white (binarization) or
dominant-background (otherwise) margins *with* dewarping. **ACCIDENTAL drift**, pre-fork.
Unification risk: whichever behavior the unified code picks changes output for one of the two mode
families; the divergence is user-visible in shipped output files, so it must be decided (and
probably characterized-tested) before merging, not silently "fixed" in passing.

**D2 — `bwContent` released before use on the original-background path (HIGH; latent crash,
non-dewarp only).**
`bwContent.release()` at **1558** null-swaps the image (`BinaryImage::release`,
`src/imageproc/BinaryImage.h:278-282`). At **1564**, `reserveBlackAndWhite(maybeNormalized,
bwContent.inverted())` runs when `originalBackground()`: `inverted()` on a null image returns a
null `BinaryImage` (`BinaryImage.cpp:227-230`), whose `data()` returns `nullptr`
(`BinaryImage.cpp:485-490`), and the pixel loop in `reserveBlackAndWhite`
(`OutputGenerator.cpp:463-482`) dereferences `maskLine[x >> 5]` on the first row →
**null-pointer dereference on a worker thread**. Line **1572** would then `rasterOp` from the same
null image. Reachability: `ORIGINAL_BACKGROUND` requires MIXED mode + split output + "B&W
foreground" splitting + the `originalBackgroundCB` checkbox, all live in the UI
(`RenderParams.cpp:19-27`; `output/OptionsWidget.cpp:1240,1423-1425`). The dewarp twin is correct:
`dewarpedBwContent` is *not* released and is used at 1962 and 1988–1989. History: at upstream
`4ccd9a44` there was no release before the use; upstream **`545ef72c`** ("1.0.19: feature #50:
foreground and background zones") introduced the early release (its own lines 1420→1426→1433 show
release-then-use) — **inherited upstream bug, not fork drift**, statically traced, not
runtime-reproduced. Unification note: the two drivers are *not* behaviorally equivalent here;
unifying on the with-driver shape silently fixes a crash — good, but it must be recorded as a
behavior change, and if the path never crashes in practice, that would prove the configuration is
unreachable in the fork and the finding downgrades to dead code.

**D3 — Wiener denoise ordering vs background-color measurement (MEDIUM).**
Same commit (`ab6018d9`), two different placements (1399–1402 pre-bg-recompute on the cropped
working image; 1695–1698 post-bg-recompute on the full original). The dewarp path measures fill
color from raw pixels, the plain path from denoised pixels; with a non-zero `wienerCoef` the chosen
`m_outsideBackgroundColor` can differ between modes for the same page. Also a cost asymmetry (full
frame vs crop). Coordinate-space difference is INTENTIONAL (dewarp samples the original); the
ordering difference is ACCIDENTAL-looking. Unifying forces a choice that shifts margin fill color
in one mode.

**D4 — Cancellation checkpoint missing after 1910–1912 (LOW).**
Trivial ACCIDENTAL gap; unification would silently *add* a checkpoint — behaviorally safe, but
worth noting because cancellation timing is observable (partial files, quit latency).

**D5 — Persisted Settings writes only in the dewarp driver (MEDIUM, structural).**
`m_settings->setDewarpingOptions(m_pageId, m_dewarpingOptions)` at 1783 stores the measured
post-deskew angle *during rendering*; plus the `distortionModel` out-param (1759/1761/1776) that
`output::Task` persists. The plain driver has no persistent side effects of its own (both share
`processPictureZones`' writes — `setPictureZones`/`setOutputProcessingParams`, see §1.3). A unified
driver's side-effect set is the union; staleness comparison (`OutputImageParams`) keys on some of
these values, so moving a write changes when pages regenerate. INTENTIONAL feature, hazardous seam.

**Everything else** (steps 4–8, 10, 13 above) is INTENTIONAL divergence forced by one fact:
*without dewarping, working-CS results can be lifted into output space with one affine rasterOp;
with dewarping, every artifact (content, mask, margins) must itself be dewarped, and binarization
must happen after resampling.* These are trivially parameterized only after someone makes the
coordinate context (`working CS + affine lift` vs `output CS + mapper`) an explicit object — which
is exactly the "pipeline stage contracts" decision the audit says must precede any split (§4.2).

### 1.3 Member-soup coupling: what each driver touches

Direct textual references inside each driver body (grep-counted over 1386–1645 / 1647–2010; a
member may additionally be touched *transitively* via helper methods — noted below):

| Member | without | with | Notes |
|---|---|---|---|
| `m_renderParams` | 25 | 28 | The de-facto control bus of both drivers |
| `m_dbg` | 16 | 28 | |
| `m_status` | 10 | 18 | Cancellation only |
| `m_outsideBackgroundColor` | 10 | 14 | **WRITTEN by both** (without: 1405, 1535, 1595–1603; with: 1689, 1921, 1971–1973; plus `processImpl` 1374) — the one mutable scalar threading the whole render |
| `m_xform` | 7 | 11 | |
| `m_croppedContentRect` | 7 | 3 | |
| `m_contentRectInWorkingCs` | 6 | 1 | Working-CS lift (without) |
| `m_blackOnWhite` | 5 | 4 | |
| `m_workingBoundingRect` | 5 | 9 | |
| `m_targetSize` | 8 | 2 | |
| `m_colorParams` | 4 | 2 | |
| `m_dpi` | 3 | 5 | |
| `m_contentAreaInWorkingCs` | 3 | 1 | |
| `m_despeckleLevel` | 2 | 2 | |
| `m_outCropAreaInOriginalCs` | 1 | 2 | |
| `m_inputOrigImage` / `m_inputGrayImage` | 1 / 1 | 5 / 5 | Dewarp re-samples originals repeatedly |
| `m_colorOriginal` | 1 | 4 | |
| `m_continuousToneRegions` / `m_pictureFrames` | 1 / 1 | 1 / 1 | Fork features, kept in sync |
| `m_outCropAreaInWorkingCs` | 1 | 0 | without-only |
| `m_outRect` | 1 | 3 | |
| `m_preCropAreaInOriginalCs` / `m_contentAreaInOriginalCs` | 0 / 0 | 1 / 1 | with-only |
| `m_dewarpingOptions` | 0 | 5 | **WRITTEN** (1773, 1777) with-only |
| `m_settings` / `m_pageId` | 0 / 0 | 1 / 1 | **`setDewarpingOptions` write** (1783) with-only |

Transitive access shared by both drivers (invisible in the counts above): `transformToWorkingCs`
(3078–3101) reads `m_inputOrigImage`, `m_inputGrayImage`, `m_colorOriginal`,
`m_outsideBackgroundColor`, `m_status`; `processPictureZones` (def near 3103) reads
`m_pictureShapeOptions`, **writes `m_outputProcessingParams`** and **persists via
`m_settings->setPictureZones` / `setOutputProcessingParams`** — i.e., *both* drivers already have
persistent side effects through this shared helper; `binarize`/`calcBinarizationThreshold`/
`maybeDespeckleInPlace`/`morphologicalSmoothInPlace` read `m_colorParams`, `m_dbg`, `m_status`,
`m_dpi`. Members untouched by either driver directly or transitively in these paths:
`m_contentRect`, `m_splittingOptions` (consumed at `initParams` time into `m_renderParams`),
`m_preCropArea`, `m_croppedContentArea`, `m_outCropArea`, `m_blank` (handled in `processImpl`).

Out-parameter mutations: both drivers mutate `pictureZones` (auto-zone insertion via
`processPictureZones`) and fill `autoPictureMask`/`specklesImage`; only the with-driver mutates
`distortionModel`.

---

## PART 2 — Confirmation-with-mechanism of the audit's top defects

### (a) DeviationProvider data race — **CONFIRMED, with one correction to the stated worst case**

**Worker-side mutation sites (locked).** `deskew::Settings::setPageParams`
(`src/core/filters/deskew/Settings.cpp:51-55`) takes `QMutexLocker locker(&m_mutex)` then calls
`m_deviationProvider.addOrUpdate(pageId)`; likewise `setDegrees` (:74-80), `clearPageParams`
(:57-61), `performRelinking` (:32-49), `clear` (:26-30). `addOrUpdate(key)`
(`src/core/DeviationProvider.h:91-95`) writes `m_needUpdate = true` and inserts into
`m_keyValueMap`, invoking the capture-`this` lambda (`Settings.cpp:14-21`) that reads
`m_perPageParams` — safe, same lock. Called from worker threads: `deskew/Task.cpp:104,120` run
inside `Task::process` on the `WorkerThreadPool`. The same pattern exists in select_content,
page_box, page_layout (Settings.cpp files each pair `mapSetValue` + `addOrUpdate` under their
mutex).

**GUI-side lock-free read sites.** `Settings::deviationProvider()` returns a bare `const&`
**with no lock** (`deskew/Settings.cpp:87-89` — verified: no `QMutexLocker` in the accessor).
Consumers, all main-thread:

1. Four `CacheDrivenTask::process` calls to `isDeviant`: `deskew/CacheDrivenTask.cpp:53`,
   `select_content/CacheDrivenTask.cpp:60`, `page_box/CacheDrivenTask.cpp:53`,
   `page_layout/CacheDrivenTask.cpp:77`. Thread affinity: cache-driven tasks execute inside
   `ThumbnailFactory::get` (`src/core/ThumbnailFactory.cpp:38-42`) called synchronously from
   `ThumbnailSequence.cpp:1357` (`m_factory->get(pageInfo)`) — thumbnail (re)creation on the GUI
   thread. During batch, `MainWindow::filterResult` invalidates the finished page's thumbnail after
   *every* page, so this fires continuously while other pool workers are still writing.
2. Four `Filter::pageOrderOptions` sites hand the same bare reference to
   `OrderByDeviationProvider` (`deskew/Filter.cpp:29`, `select_content/Filter.cpp:36`,
   `page_box/Filter.cpp:34`, `page_layout/Filter.cpp:34`), which stores a **pointer**
   (`src/core/OrderByDeviationProvider.h:18` `const DeviationProvider<PageId>*
   m_deviationProvider`) and calls `getDeviationValue` in `precedes()` during GUI-side sort
   comparisons.

That is the audit's "~8 racing read sites across 4 filters" — count confirmed exactly
(4 CacheDrivenTask + 4 Filter).

**The unlocked read path really takes no lock.** `isDeviant` (`DeviationProvider.h:54-70`) does
unlocked `find` (55), `size` (58), `at` (62), then `update()` (67). `update()` (115-146)
checks/clears `mutable m_needUpdate` and, when stale, **iterates the whole `m_keyValueMap` twice**
(126-131, 137-141) while writing `mutable m_meanValue` / `m_standardDeviation` (132, 142, 145).
None of these touch any mutex; `DeviationProvider` has no mutex to take.

**Concrete corruption scenario (corrected).** During a batch, worker W executes `setPageParams` →
`m_keyValueMap[key] = …` under the Settings mutex; a map insert can trigger an `unordered_map`
**rehash**. Concurrently the GUI thread, repainting a thumbnail for an earlier page, is inside
`update()` iterating that same map with no lock. The GUI thread's bucket/node pointers dangle
mid-rehash → **use-after-free reads on the GUI thread** → crash (most likely) or garbage
`m_meanValue`/`m_standardDeviation` → wrong deviance highlighting. Additional races: torn unlocked
reads of the `double` cache fields, and the `m_needUpdate` flag written `true` by workers and
`false` by the GUI with no ordering — a lost `true` leaves statistics permanently stale until the
next write. All of this is TSan-definite.

**Correction to the audit's wording:** §5.5-1 says the GUI thread is "rewriting the provider's
internal map", and §9-Q15 escalates to "heap corruption in a Settings object shared by every
page". Neither is quite right: **the GUI side never writes the map** — it writes only the three
`mutable` scalar members; the map is mutated exclusively under the Settings mutex by workers. So
the realistic worst case is a GUI-thread crash from a dangling-bucket read (plus
wrong-order/wrong-highlight symptoms), **not** allocator/heap corruption of the Settings object.
The race itself, its every-batch exercise, and its severity as a crasher stand confirmed; only the
corruption mechanism was overstated.

### (b) PDF import render DPI not serialized — **CONFIRMED, and worse than stated: reopen silently rewrites project metadata**

**Import.** The DPI dialog result reaches `PdfReader::setImportDpi(path, dpi)` from exactly three
places: `MainWindow.cpp:3086`, `AppController.cpp:88`, `AppController.cpp:255` (plus tests).
`setImportDpi` (`PdfReader.mm:556-563`) stores into the file-static `s_importDpiMap` under
`s_importDpiMutex` and invalidates the decode cache. MainWindow also copies it into
`output::Settings::m_defaultDpi` via `m_stages->outputFilter()->setDefaultDpi`
(`MainWindow.cpp:3150-3154` and `3196-3198`; member at `output/Settings.h:133`, used as the initial
per-page output DPI at `output/Settings.cpp:95`).

**Render.** Every decode goes through `ImageLoader::load` (`ImageLoader.cpp:262-269`):
`pdfRenderDpi = PdfReader::getImportDpi(filePath)`, which returns the map entry or
**`DEFAULT_RENDER_DPI = 300`** (`PdfReader.mm:565-572`, `PdfReader.h:19`); the DPI is part of the
decode-cache key and is passed to `PdfReader::readImage`.

**Save.** Grep of `ProjectWriter.cpp`, `ProjectReader.cpp`, and `output/Filter.cpp` for any
import/render-DPI attribute: **zero hits**. Neither `s_importDpiMap` nor `m_defaultDpi` has any
serialized representation. The `<image>` elements carry `ImageMetadata` (size+DPI as measured at
import resolution).

**Reopen.** `ProjectReader` and `ProjectOpeningContext` never call `setImportDpi` (grep: dialog
sites and tests are the only callers). So on reopen, `getImportDpi` returns 300 and every page
renders at 300 DPI regardless of the import choice.

**What the audit missed — the destructive follow-on.** `LoadFileTask::operator()`
(`LoadFileTask.cpp:61-71`) then calls `updateImageSizeIfChanged(image)` (:80-93): because the
300-DPI render's pixel size differs from the stored 600-DPI metadata size, it **silently rewrites
`ProjectPages`' stored `ImageMetadata` size to the new, smaller size** ("The user might just
replace a file with another one"), while `overrideDpi` (:95-101) stamps the *stored* (600) DPI onto
the shrunken image. Every geometry parameter saved in image coordinates at 600 DPI — page boxes,
content boxes, zones, distortion models — now misaligns by 2×, and the **next save persists the
rewritten metadata**, making the damage permanent even if a future build learned to restore the
import DPI. F1 confirmed and upgraded from "renders at the wrong DPI" to "reopen corrupts saved
geometry via the file-replacement heuristic". (Statically traced; runtime repro still outstanding,
as the audit already noted.)

### (c) TIFF compression no-op — **CONFIRMED in full, every link verified**

The UI chain that *looks* like it works: `finalize/OptionsWidget.cpp:291-296` `compressionChanged`
→ `m_settings->setTiffCompression` (`finalize/Settings.h:139` — in-memory; `finalize::Filter::
saveSettings` writes only `pictureDetectionSensitivity` + per-page params,
`finalize/Filter.cpp:66-74`, so it resets on reload) and emits `tiffCompressionSettingChanged` →
connected at `MainWindow.cpp:1215-1216` → `MainWindow::tiffCompressionSettingChanged` (:1748-1751)
→ `m_outFileNameGen.setTiffCompression` (`OutputFileNameGenerator.h:55`).

The dead end: **`OutputFileNameGenerator::tiffCompression()` (`OutputFileNameGenerator.h:54`) has
zero callers** in `src/` (grep over the tree: only the declaration, the setter, and the member).
The value goes in and never comes out.

The actual write path: `output/Task.cpp:55-67` `writeOutputImage` — TIFF branch calls
`TiffWriter::writeImage(filePath, image)` with no compression argument, above the comment (:64-65)
"*TiffWriter uses ApplicationSettings internally for compression / The finalize settings
compression is synced to ApplicationSettings when changed*". The first half is true; the second
half is **false**: `ApplicationSettings::setTiffBwCompression` / `setTiffColorCompression` have
**zero call sites anywhere in `src/`** (grep; only the definitions in
`ApplicationSettings.cpp:103-112`). `TiffWriter` (`src/core/TiffWriter.cpp:245,248,288,330`) reads
`getTiffBwCompression()`/`getTiffColorCompression()` → QSettings keys `settings/bw_compression` /
`settings/color_compression` (`ApplicationSettings.cpp:41-42`), defaulting to
`COMPRESSION_CCITTFAX4` (=4) and `COMPRESSION_LZW` (=5) (`ApplicationSettings.cpp:14-15`) —
hand-editable INI values with no in-app writer.

The enum mismatch, concretely: `finalize::TiffCompression { LZW = 0, Deflate = 1 }`
(`finalize/Settings.h:48-51`) and `OutputTiffCompression { LZW, Deflate }`
(`OutputFileNameGenerator.h:24-27`) vs libtiff wire codes `COMPRESSION_CCITTFAX4 = 4`,
`COMPRESSION_LZW = 5`, `COMPRESSION_ADOBE_DEFLATE = 8` (`COMPRESSION_DEFLATE = 32946`). Even a
naive future "sync" that wrote the enum's raw int into the QSettings key would hand libtiff `0` or
`1` (`COMPRESSION_NONE` is 1) — i.e., the two vocabularies don't overlap on a single value.
Audit's F2/no-op claim confirmed exactly, including the false comment and the value disagreement.
(Note the neighboring format/JPEG-quality settings are *not* no-ops — `writeOutputImage` reads
`outFileNameGen.outputFormat()`/`jpegQuality()` at `output/Task.cpp:56-61`; only TIFF compression
dead-ends.)

### (d) `$TMPDIR` output-dir serialization — **CONFIRMED on all four claims, with two precisions**

**Naming/collision.** `finalize::Settings::getTempOutputDir(projectPath)`
(`finalize/Settings.cpp:248-253`): `$TMPDIR/scantailor-spectre-<md5(sourcePath).hex[:8]>`, where
the argument at both call sites is the **source PDF path** (`MainWindow.cpp:3143`, `:3189`). Hash
input contains no project identity, no session nonce → two projects (sequential, or two app
instances) created from the same PDF share one directory. Collision claim confirmed at the naming
level. `getEffectiveOutputDir` (:241-246) would prefer `m_outputPath` if `m_preserveOutput` were
ever set, but both setters have zero callers (audit F2, re-confirmed by grep) — the temp dir
always wins.

**Serialization.** `ProjectWriter::write` stores `outputDirectory =
toRelativeIfSameVolume(projectDir, m_outFileNameGen.outDir())` (`ProjectWriter.cpp:111-112`;
converter :88-99). If QStorageInfo reports the project's volume and `/var/folders/...` as the same
root (typical on one-volume macOS where both live on the Data volume), the stored value is a
**relative** path climbing to `/var/folders/…`; cross-volume it stays absolute. Either way the
temp path is serialized. Confirmed; the relative-vs-absolute outcome depends on QStorageInfo's
firmlink handling and was not runtime-verified (also flagged unverified by the audit).

**Reopen: nothing recreates it.** `ProjectReader` reads the attribute verbatim through
`resolvePath` (`ProjectReader.cpp:31`) → `MainWindow::projectOpened` (:3309-3310) →
`switchToNewProject`, which only calls `Utils::maybeCreateCacheDir(outDir)` (:707-709) =
`QDir(outputDir).mkdir("cache")` (`src/core/Utils.cpp:40-45`) — `mkdir` of a *child* fails
silently when `outputDir` itself no longer exists; no `mkpath(outDir)` anywhere on the open path.
Confirmed.

**Deletion on close.** `closeProjectWithoutSaving` (:3633-3639) → `cleanupTempOutputFiles`
(:3641-3689): guards are (i) `preserveOutput()` — always false, dead; (ii)
`isSpectreTempOutputDir` prefix check (:249-257, `$TMPDIR/scantailor-spectre-` prefix); (iii)
`showTempCleanupWarning()` — a cancellable dialog, **skippable via the
`settings/temp_cleanup_warning` opt-out** (:3691-3697); then `QDir::removeRecursively()`. Two
precisions the audit didn't spell out: the deletion targets **`m_defaultOutDir`** — set at `:739`
to whatever `outDir` `switchToNewProject` received, which on reopen *is* the deserialized temp
path (comment at :3659-3661 explains it deliberately avoids re-hashing after Save-As) — and the
recursive delete is dialog-guarded, so "closing one recursively deletes it" holds by default but
the user can cancel. Net: the collision consequence stands — close project A (from `book.pdf`),
accept the dialog, and project B's outputs from the same `book.pdf` are gone; B's staleness
contract will force reprocessing rather than corruption, but all rendered output is lost. All
statically traced; the two-simultaneous-windows variant remains runtime-unverified (audit Q14).

### (e) Unknown-XML-element drop + version==4 wall — **CONFIRMED on every sub-claim**

**Round-trip drops everything unknown.** `ProjectWriter::write` (`ProjectWriter.cpp:104-138`)
builds a **fresh `QDomDocument`** entirely from in-memory state (root attrs at 110-112, then
`processDirectories/Files/Images/Pages`, disambiguator, and each filter's `saveSettings`
regenerating its element). There is no pass-through of unrecognized nodes; an element or attribute
the reader didn't map into a live struct has no carrier and vanishes on the next save. Preserved:
exactly the schema the current build reads (root attrs incl. the `selected` page,
directories/files/images/pages, disambiguation, the 10 filter dialects). Dropped: everything else.

**Version wall, both directions, profiles included.** `ProjectReader` ctor bails unless
`version.toInt() == PROJECT_VERSION` (`ProjectReader.cpp:26-29`); `PROJECT_VERSION = 4`
(`version.h.in:8`). `success()` is `m_pages != nullptr` (`ProjectReader.h:38`), so the bail leaves
the reader failed; `ProjectOpeningContext::proceed` (`src/app/ProjectOpeningContext.cpp:21-31`)
then shows "not compatible with the current application version" when a version attribute is
present, else "Unable to interpret". Older *and* newer both rejected; the only compat shim is the
per-file `multiPage` attribute (`ProjectReader.cpp:149-153`). Defaults profiles share the
constant: `DefaultParamsProfileManager.cpp:78` rejects `.stp` files whose `version !=
PROJECT_VERSION` (INCOMPATIBLE_VERSION_ERROR) and `:96` writes it — a project-format bump
invalidates every user profile. Confirmed.

**Malformed per-page element: silent-then-destructive.** Example traced in full —
`output::Filter::loadSettings` (`output/Filter.cpp:80-101`): iterates `<page>` children and
`continue`s past non-elements, wrong tag names, unparsable `id`, and unresolvable `PageId`; the
page keeps default params, and the open still succeeds (per-page tolerance; the open fails only on
version mismatch / missing top-level structure / no valid images). On the next save,
`saveSettings` regenerates the section from memory — the malformed-but-possibly-recoverable
original element is **permanently erased**. The same `continue` shape recurs across the other
filters' `loadSettings`. Audit §6.4 and Q10 confirmed as written.

---

## Could not verify (honest list)

1. **D2 crash not runtime-reproduced.** The null-deref chain (1558→1564→463-482) is airtight
   statically, but "MIXED + split output + B&W foreground + original background, no dewarp" was not
   executed. If that configuration demonstrably works in the shipped app, the trace is wrong
   somewhere (most plausibly the configuration is unreachable) and D2 must be re-graded before it
   is used to justify anything.
2. **QStorageInfo same-volume behavior for `/var/folders` vs `/Users`** under APFS firmlinks
   (decides whether the serialized temp path is relative or absolute) — not executed; both
   variants serialize the temp path, so the finding stands either way.
3. **F1 user-visible symptom** (2× geometry misalignment + metadata rewrite on reopen of a 600-DPI
   import) — statically traced through `LoadFileTask`; no runtime repro. The 2.0b2 "PDF import
   fixes" changelog was not audited commit-by-commit for adjacent behavior.
4. **Whether output writes fail loudly or confusingly** when the serialized temp `outputDirectory`
   no longer exists (audit Q12) — traced to "nothing mkpaths it"; failure UX not observed.
5. **Worker-side deviation writes in select_content / page_box / page_layout** were
   pattern-matched (same `mapSetValue` + `addOrUpdate` under mutex shape as deskew), not read
   line-by-line; deskew was verified fully. page_layout's cpp-private mutex arrangement was taken
   from the audit.
6. **Whether any additional `Processor` state flows between the drivers via helpers not read
   line-by-line** (`initParams`, `calcAreas`, `buildAutoDistortionModel`,
   `buildMarginalDistortionModel` were skimmed for signatures/side effects only). The member
   table's transitive rows cover the helpers both drivers call in their bodies.
7. **Upstream status of D1/D2** in current scantailor-advanced master (whether either was later
   fixed upstream) — not checked; both were dated within this repo's history only (`87af4841`,
   `545ef72c`).

## Implications for stage ordering (observations only; the proposal is `docs/REFACTOR_PROPOSAL.md`)

- **The two drivers are not refactor-equivalent copies.** Two behavioral asymmetries (D1
  fill-black, D2 release-before-use) mean "unify the drivers" is not a pure structural move — it
  either freezes or changes user-visible behavior. Characterization coverage (golden-image renders
  across the mode matrix: BW/mixed/color × dewarp on/off × split/original-background × fill
  white/black) would have to exist *before* any unification stage, and D1/D2 need explicit owner
  decisions (which behavior is correct) rather than silent resolution inside a refactor diff.
- **The deviation race (a) is independent of everything else** in this list: it lives in core +
  four filters' Settings accessors and touches neither OutputGenerator nor persistence. Nothing
  here suggests it must wait on, or precede, output-stage work — but any stage that adds tests
  exercising batch + thumbnails will be running on top of a known crasher until it is fixed.
- **Persistence defects (b), (d), (e) interact:** F1's metadata rewrite and (e)'s
  regenerate-on-save both mean *opening a project already mutates what the next save writes*. Any
  stage that adds project-file round-trip fixtures should capture files from disk **before** the
  app has opened them, and treat "open→save→diff" as a lossy operation by design until F1/(e)
  decisions are made.
- **(c) is self-contained**: the TIFF-compression dead end is confined to finalize UI ↔
  OutputFileNameGenerator ↔ ApplicationSettings/TiffWriter and can be decided (audit product
  decision 9) without touching OutputGenerator at all.
- **The dead `NORMALIZE_ILLUMINATION_COLOR` flag** (RenderParams) is a small, safe simplification
  opportunity *inside* the drivers' shared vocabulary — but removing it changes both drivers' text
  and would pollute any diff-based characterization of the D1/D2 decisions if done in the same
  stage.
