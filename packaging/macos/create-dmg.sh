#!/bin/bash
#
# create-dmg.sh - Create a distributable DMG for ScanTailor Spectre
#
# Usage: ./create-dmg.sh [build_directory] [--sign] [--notarize]
#
# Options:
#   --sign       Sign the app bundle and DMG with Developer ID
#   --notarize   Submit to Apple for notarization (implies --sign)
#
# If build_directory is not specified, assumes ../../build

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Configuration
DEVELOPER_ID="${STS_CODESIGN_IDENTITY}"
KEYCHAIN_PROFILE="notarytool"
APP_NAME="ScanTailor Spectre"

# Parse arguments
BUILD_DIR=""
DO_SIGN=false
DO_NOTARIZE=false

for arg in "$@"; do
    case $arg in
        --sign)
            DO_SIGN=true
            ;;
        --notarize)
            DO_NOTARIZE=true
            DO_SIGN=true  # Notarization requires signing
            ;;
        *)
            if [ -z "$BUILD_DIR" ]; then
                BUILD_DIR="$arg"
            fi
            ;;
    esac
done

# Default build directory
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found: $BUILD_DIR"
    echo "Usage: $0 [build_directory] [--sign] [--notarize]"
    exit 1
fi

# Find the app bundle
APP_BUNDLE="$BUILD_DIR/${APP_NAME}.app"
if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: App bundle not found: $APP_BUNDLE"
    echo "Please build the project first with: cmake --build $BUILD_DIR"
    exit 1
fi

# Get version from the app bundle
VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP_BUNDLE/Contents/Info.plist" 2>/dev/null || echo "unknown")
echo "Creating DMG for ${APP_NAME} version $VERSION"

# Clear extended attributes
echo "Clearing extended attributes..."
xattr -cr "$APP_BUNDLE"

# Sign the app bundle if requested
if [ "$DO_SIGN" = true ]; then
    echo "Signing app bundle with: $DEVELOPER_ID"
    codesign --force --deep --options runtime --sign "$DEVELOPER_ID" "$APP_BUNDLE"
    echo "Verifying signature..."
    codesign --verify --deep --strict "$APP_BUNDLE"
fi

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

# Create the DMG
DMG_NAME="ScanTailor-Spectre-${VERSION}.dmg"
DMG_PATH="$SCRIPT_DIR/$DMG_NAME"

echo "Creating DMG: $DMG_NAME"

# Remove existing DMG if present
rm -f "$DMG_PATH"

# Create the DMG
hdiutil create \
    -volname "$APP_NAME" \
    -srcfolder "$DMG_TEMP_DIR" \
    -ov \
    -format UDZO \
    "$DMG_PATH"

# Sign the DMG if requested
if [ "$DO_SIGN" = true ]; then
    echo "Signing DMG..."
    codesign --force --sign "$DEVELOPER_ID" "$DMG_PATH"
fi

# Notarize if requested
if [ "$DO_NOTARIZE" = true ]; then
    echo "Submitting for notarization..."
    xcrun notarytool submit "$DMG_PATH" \
        --keychain-profile "$KEYCHAIN_PROFILE" \
        --wait

    echo "Stapling notarization ticket..."
    xcrun stapler staple "$DMG_PATH"

    echo "Verifying notarization..."
    spctl --assess --type open --context context:primary-signature -v "$DMG_PATH"
fi

echo ""
echo "DMG created successfully: $DMG_PATH"
echo ""
echo "To install:"
echo "  1. Open the DMG"
echo "  2. Drag '${APP_NAME}.app' to the Applications folder"
echo ""

# Verify final result
if [ "$DO_SIGN" = true ]; then
    echo "Signature verification:"
    codesign -dv --verbose=2 "$DMG_PATH" 2>&1 | grep -E "Authority|Signature"
fi
