# OutputGenerator golden fixtures (Stage 1.5.1)

Characterization goldens for `OutputGenerator::process`, pinning what BOTH
render drivers (`processWithoutDewarping` / `processWithDewarping`) produce
TODAY — including the known divergences D1 (fill-black asymmetry) and D3
(wiener/bg-color ordering). See `TestOutputGolden.cpp` for the case matrix and
`docs/REFACTOR_PREREQS.md` Part 1 for the divergence analysis these tests make
executable. Stage 2A (driver unification) is gated on this suite staying green
for every cell its plan declares unchanged.

## Layout

- `text-page.png`, `mixed-page.png` — frozen synthetic source pages, written
  once by `golden_fixture_writer` (deterministic pixel arithmetic, fixed LCG
  seeds, no QPainter/fonts — see `GoldenSourcePages.cpp`). Never regenerate
  casually; they are inputs, not expectations.
- `goldens/<case>.png` — the pinned render for each case.
- `goldens/<case>.meta` — the pinned in-memory `QImage::Format` (PNG round
  trips can change formats, so the format pin is explicit) plus size.
- `goldens/<case>_automask.png` — the pinned auto picture-detection mask for
  MIXED cases.

## What the goldens are (and are not)

- **CPU-path renders.** `output_golden_tests` asserts Metal is unavailable in
  the test binary (no `default.metallib` next to the executable), so a change
  in acceleration gating fails loudly instead of flaking.
- **Pixel-exact on this machine's toolchain.** Comparison is byte-exact in
  ARGB32 space. A different Qt/libc++/CPU may legitimately produce different
  pixels; if that happens, regenerate deliberately on the canonical dev
  machine and review the diff — do not loosen the comparison silently.
- **MANUAL dewarp only.** The dewarp cases use a fixed, committed distortion
  model (constants in `OutputGoldenHarness.cpp`), so the dewarp driver runs
  deterministically. AUTO/MARGINAL model *building* is content-dependent and
  is NOT pinned here (recorded gap).
- **Warts included.** The goldens pin current behavior, defects and all:
  fill color is ignored by the pure-BW early-return path in both drivers;
  picture detection misses the low-contrast part of the photo block. None of
  that is endorsed — it is frozen so Stage 2A changes are visible.
  (D1 — FILL_BLACK ignored under dewarping — was fixed by porting the
  filling-color switch into the dewarp driver; the `dewarp_*_fill_black`
  goldens were regenerated with that fix and now show black margins like
  their non-dewarp siblings.)

## D2 (fixed by QW1)

`mixed + split output + B&W foreground + original background`, non-dewarp,
used to crash (null-pointer dereference, release-before-use of `bwContent` —
prereqs D2, runtime-confirmed by the pre-QW1 child-process probe). QW1
mirrored the correct dewarp-driver shape, and the configuration now renders:
it is the `mixed_split_orig_bg` golden cell, and
`TestOutputGoldenD2Probe.cpp` additionally cross-checks it in-process against
its dewarp sibling golden (`dewarp_mixed_split_orig_bg`) for structural
plausibility.

## Regeneration recipe

```bash
cmake --build build --target output_golden_tests -j$(sysctl -n hw.ncpu)
cd build && ST_GOLDEN_REGEN=1 ./output_golden_tests --run_test=OutputGoldenTestSuite/golden_matrix
```

This rewrites `goldens/` in the source tree. Then run the suite WITHOUT the
env var to confirm green, review every changed golden by eye, and commit the
regeneration together with the change that motivated it. Source pages are
regenerated separately (`build/golden_fixture_writer src/core/tests/fixtures/golden`)
and should essentially never be.
