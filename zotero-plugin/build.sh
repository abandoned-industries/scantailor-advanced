#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST="$ROOT/dist"
XPI="$DIST/st-spectre-loop.xpi"
BUNDLED_NODE_ROOT="${CODEX_WORKSPACE_NODE_ROOT:-${HOME}/.cache/codex-runtimes/codex-primary-runtime/dependencies/node}"

if [[ -x "$BUNDLED_NODE_ROOT/bin/node" && -d "$BUNDLED_NODE_ROOT/node_modules/playwright" ]]; then
	NODE_BIN="$BUNDLED_NODE_ROOT/bin/node"
	export NODE_PATH="$BUNDLED_NODE_ROOT/node_modules${NODE_PATH:+:$NODE_PATH}"
else
	NODE_BIN="$(command -v node || true)"
	if [[ -z "$NODE_BIN" ]] || ! "$NODE_BIN" -e 'require("playwright")' >/dev/null 2>&1; then
		echo "Error: Zotero dialog tests require Node.js and Playwright." >&2
		echo "Use the bundled workspace runtime or install Playwright locally." >&2
		exit 1
	fi
fi

"$NODE_BIN" "$ROOT/test/smoke.js"
"$NODE_BIN" "$ROOT/test/dialog.js"
"$NODE_BIN" "$ROOT/test/layout.js"

mkdir -p "$DIST"
rm -f "$XPI"

(
	cd "$ROOT"
	/usr/bin/zip -qr "$XPI" manifest.json bootstrap.js chrome.manifest content \
		-x '*/.DS_Store'
)

echo "Built $XPI"
