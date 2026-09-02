#!/bin/bash
#
# create-dmg.sh - Stage an UNSIGNED, timestamped DMG of ScanTailor Spectre
#                 for local testing.
#
# ============================================================================
# THIS SCRIPT DOES NOT SIGN, NOTARIZE, OR STAPLE ANYTHING.
#
# Release artifacts are produced exclusively by following SIGNING.md at the
# repo root: Developer ID component-by-component signing (never --deep),
# notarization with the one machine-wide keychain profile "notary",
# stapling, identically timestamped ZIP + DMG, and mounted-DMG
# verification. Do not distribute a DMG produced by this script.
# ============================================================================
#
# Usage: ./create-dmg.sh [build_directory]
#
# If build_directory is not specified, assumes ../../build

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_NAME="ScanTailor Spectre"

BUILD_DIR="${1:-$PROJECT_ROOT/build}"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found: $BUILD_DIR"
    echo "Usage: $0 [build_directory]"
    exit 1
fi

# Find the app bundle (produced by the scantailor_bundle target)
APP_BUNDLE="$BUILD_DIR/${APP_NAME}.app"
if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: App bundle not found: $APP_BUNDLE"
    echo "Build it first with: cmake --build \"$BUILD_DIR\" --target scantailor_bundle"
    exit 1
fi

# Get version from the app bundle
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP_BUNDLE/Contents/Info.plist" 2>/dev/null || echo "unknown")
STAMP=$(date +%Y%m%d-%H%M)
echo "Creating unsigned test DMG for ${APP_NAME} version $VERSION ($STAMP)"

# Clear extended attributes
echo "Clearing extended attributes..."
xattr -cr "$APP_BUNDLE"

# Create a temporary directory for DMG contents
DMG_TEMP_DIR=$(mktemp -d)
trap "rm -rf $DMG_TEMP_DIR" EXIT

# Copy the app bundle
echo "Copying app bundle..."
cp -R "$APP_BUNDLE" "$DMG_TEMP_DIR/"

# Include the exact Zotero companion plugin already validated, copied, and
# signed into the app bundle by the scantailor_bundle target.
XPI_PATH="$APP_BUNDLE/Contents/Resources/st-spectre-loop.xpi"
if [ ! -f "$XPI_PATH" ]; then
    echo "Error: Bundled Zotero plugin not found: $XPI_PATH"
    echo "Run $PROJECT_ROOT/zotero-plugin/build.sh, then rebuild the scantailor_bundle target."
    exit 1
fi

manifest_version() {
    /usr/bin/sed -n 's/^[[:space:]]*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*$/\1/p'
}

PLUGIN_MANIFEST_PATH="$PROJECT_ROOT/zotero-plugin/manifest.json"
SOURCE_PLUGIN_VERSION=$(manifest_version < "$PLUGIN_MANIFEST_PATH")
XPI_PLUGIN_VERSION=$(/usr/bin/unzip -p "$XPI_PATH" manifest.json | manifest_version)
if [ -z "$SOURCE_PLUGIN_VERSION" ] || [ -z "$XPI_PLUGIN_VERSION" ]; then
    echo "Error: Could not read the Zotero plugin version from the source manifest or XPI."
    echo "Run $PROJECT_ROOT/zotero-plugin/build.sh, then rebuild the scantailor_bundle target."
    exit 1
fi
if [ "$SOURCE_PLUGIN_VERSION" != "$XPI_PLUGIN_VERSION" ]; then
    echo "Error: Bundled Zotero plugin XPI is stale (XPI $XPI_PLUGIN_VERSION, source $SOURCE_PLUGIN_VERSION)."
    echo "Run $PROJECT_ROOT/zotero-plugin/build.sh, then rebuild the scantailor_bundle target."
    exit 1
fi
echo "Copying Zotero plugin..."
cp "$XPI_PATH" "$DMG_TEMP_DIR/"

# Create a symbolic link to /Applications
ln -s /Applications "$DMG_TEMP_DIR/Applications"

# Create the DMG (timestamped, per the release naming convention)
DMG_NAME="ScanTailor-Spectre-${VERSION}-${STAMP}.dmg"
DMG_PATH="$SCRIPT_DIR/$DMG_NAME"

echo "Creating DMG: $DMG_NAME"

# Remove existing DMG if present
rm -f "$DMG_PATH"

hdiutil create \
    -volname "$APP_NAME" \
    -srcfolder "$DMG_TEMP_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

echo ""
echo "Unsigned test DMG created: $DMG_PATH"
echo ""
echo "This DMG is for local testing only. To produce a release artifact,"
echo "follow SIGNING.md (sign, notarize with keychain profile 'notary',"
echo "staple, and verify the mounted DMG)."
