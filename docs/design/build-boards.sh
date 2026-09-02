#!/usr/bin/env bash
# Assemble the Revision 1 design boards into one canvas page.
# Sources: docs/design/rev1/*.dc.html + canvas.json. Output: docs/design/rev1/out/.
# Never hand-edit the output; regenerate with this script.
set -euo pipefail

DESIGN_SKILL_DIR="${DESIGN_SKILL_DIR:-/private/tmp/claude-501/bundled-skills/2.1.255/0bf08274c9319600f473f09b09c49b4a/design}"
SEED="$DESIGN_SKILL_DIR/seed-canvas.mjs"
TEMPLATE="$DESIGN_SKILL_DIR/payload.template.html"

if [[ ! -f "$SEED" || ! -f "$TEMPLATE" ]]; then
  echo "build-boards: design skill not found at $DESIGN_SKILL_DIR (set DESIGN_SKILL_DIR)" >&2
  exit 1
fi

cd "$(dirname "$0")/rev1"
mkdir -p out
OUT="out/scantailor-spectre-revision-1.html"

node "$SEED" \
  --template "$TEMPLATE" \
  --out "$OUT" \
  --title "ScanTailor Spectre Revision 1" \
  --artboard Main.dc.html \
  --artboard Start.dc.html \
  --artboard Finalize.dc.html \
  --artboard Batch.dc.html \
  --artboard Review.dc.html \
  --artboard Export.dc.html \
  --artboard DirectionA.dc.html \
  --artboard DirectionB.dc.html \
  --canvas canvas.json

node "$SEED" --check "$OUT"
