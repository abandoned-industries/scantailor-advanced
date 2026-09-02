# Frozen v4 project fixtures (Stage 1.5.3)

These are FROZEN `.ScanTailor` version-4 project files, committed as
characterization fixtures for the persistence layer (refactor proposal
§1.5.3). Tests (`TestFrozenProjectFixtures.cpp` in `project_roundtrip_tests`)
always copy a fixture to a temp directory before opening it; **the committed
bytes must never change**. A build that can no longer read them identically
fails the suite — that is the point.

## Contents

- `image-project/` — a two-source project (one single page, one two-page
  spread; synthetic PNG scans) with **all 10 filter sections populated with
  non-default values**, including a fix_orientation rotation of 90°. The
  expected values live in `RoundtripProjectBuilder.h` (`BuilderExpectations`).
- `pdf600-project/` — a one-page PDF-import project **frozen at 600 DPI**
  (`book-600dpi.pdf` is 144×216 pt, so the stored `ImageMetadata` is
  1200×1800 px @ 600 DPI). This is the F1 fixture, frozen BEFORE QW5 added
  the `pdfImportDpi` file attribute: it has no serialized import DPI, so
  reopening it still renders at the 300-DPI default (600×900 px) — the exact
  mismatch that triggers `LoadFileTask::updateImageSizeIfChanged`'s silent
  metadata rewrite. The test pins that legacy-absence behavior explicitly,
  and a second test drives the QW5 flow on a temp copy (set the import DPI,
  resave, reopen: renders at 600, mismatch gone). Expected values live in
  `RoundtripProjectBuilder.h` (`Pdf600Expectations`).

## Provenance / regeneration recipe

Written once (2026-08-26) by the committed generator:

```bash
cmake --build build --target project_fixture_writer -j$(sysctl -n hw.ncpu)
build/project_fixture_writer src/core/tests/fixtures/projects
```

Everything the generator emits is deterministic (fixed geometry, synthetic
images, programmatically assembled PDF). Regenerating **re-freezes** the
fixtures to the current writer's output and therefore erases their value as a
record of the freeze-date dialect — do it only deliberately (e.g. after an
approved, owner-signed format change), review the diff line by line, and
update `BuilderExpectations`/`Pdf600Expectations` in the same commit.

No personal data: all content is synthetic (see audit §10.12 on OCR text in
project XML — the OCR words here are the generator's two German sample words).
