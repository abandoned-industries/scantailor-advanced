#!/bin/bash
# Copyright (C) 2026 ScanTailor Spectre contributors
# Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

set -euo pipefail

APP_BUNDLE="${1:?Usage: sign-adhoc.sh /path/to/ScanTailor Spectre.app}"
FRAMEWORKS_DIR="$APP_BUNDLE/Contents/Frameworks"
PLUGINS_DIR="$APP_BUNDLE/Contents/PlugIns"
MAIN_EXECUTABLE="$APP_BUNDLE/Contents/MacOS/ScanTailor Spectre"
WEBENGINE_HELPER="$FRAMEWORKS_DIR/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app"

signAdHoc() {
  local component="$1"
  # `codesign --force` can retain a stale code directory after install_name_tool
  # edits a previously signed Homebrew binary. Removing first is deterministic.
  codesign --remove-signature "$component" 2>/dev/null || true
  codesign --sign - "$component"
}

# A release-signed app can leave both modern and legacy resource envelopes.
# Neither may survive into a freshly ad-hoc-signed development bundle.
rm -rf "$APP_BUNDLE/Contents/_CodeSignature"
rm -f "$APP_BUNDLE/Contents/CodeResources"

# Sign nested code before its enclosing bundle. A single --deep pass skips some
# already-signed Homebrew dylibs and is not reliable for Qt framework bundles.
while IFS= read -r binary; do
  signAdHoc "$binary"
done < <(find "$FRAMEWORKS_DIR" -type f -name 'Qt*' -path '*/Versions/A/*' -print)

while IFS= read -r dylib; do
  signAdHoc "$dylib"
done < <(find "$FRAMEWORKS_DIR" -type f -name '*.dylib' -print)

while IFS= read -r plugin; do
  signAdHoc "$plugin"
done < <(find "$PLUGINS_DIR" -type f -name '*.dylib' -print)

if [ -d "$WEBENGINE_HELPER" ]; then
  signAdHoc "$WEBENGINE_HELPER/Contents/MacOS/QtWebEngineProcess"
  signAdHoc "$WEBENGINE_HELPER"
fi

while IFS= read -r framework; do
  signAdHoc "$framework"
done < <(find "$FRAMEWORKS_DIR" -type d -name '*.framework' -print)

signAdHoc "$MAIN_EXECUTABLE"
signAdHoc "$APP_BUNDLE"
codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"
