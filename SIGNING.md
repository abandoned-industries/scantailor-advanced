# macOS Signing, Notarization, and Release

## Prerequisites

- Xcode command line tools installed
- Developer ID Application certificate in keychain
- Notarization credentials stored as keychain profile "notary"
- Identity: `Developer ID Application: Kazys Varnelis (PHCL25Z99X)`

## 1. Build

```bash
cd ~/scantailor-weasel
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DPORTABLE_VERSION=OFF
export CLANG_MODULE_CACHE_PATH=$(pwd)/build/.cache
cmake --build build --target scantailor_bundle
```

Distribution builds must set `PORTABLE_VERSION=OFF`. Portable mode places a
settings file under `Contents/MacOS`, which hardened signing rejects as loose
unsigned content. If this build tree previously used portable mode, remove the
stale `ScanTailor Spectre.app/Contents/MacOS/config` directory before signing.

The explicit `scantailor_bundle` target rebuilds the app, runs `macdeployqt`,
applies `fix-bundle-libs.sh`, normalizes bundled dylib deployment targets to
the app minimum, and ad-hoc signs the finished bundle.

## 2. Sign with Developer ID

`macdeployqt` only ad-hoc signs. For distribution you must re-sign
everything with the Developer ID certificate. Components must be signed
individually — `--deep` does not work reliably for Qt framework bundles.

```bash
cd ~/scantailor-weasel/build
IDENTITY="Developer ID Application: Kazys Varnelis (PHCL25Z99X)"
APP="ScanTailor Spectre.app"
```

### Sign in this order:

**Step 1: Framework binaries inside Versions/A**
```bash
find "$APP/Contents/Frameworks" -type f -name "Qt*" -path "*/Versions/A/*" | while read bin; do
  codesign --force --options runtime --sign "$IDENTITY" "$bin"
done
```

**Step 2: Framework bundles**
```bash
find "$APP/Contents/Frameworks" -name "*.framework" -type d | while read fw; do
  codesign --force --options runtime --sign "$IDENTITY" "$fw"
done
```

**Step 3: Dylibs**
```bash
find "$APP/Contents/Frameworks" -name "*.dylib" -type f | while read lib; do
  codesign --force --options runtime --sign "$IDENTITY" "$lib"
done
```

**Step 4: Plugins**
```bash
find "$APP/Contents/PlugIns" -name "*.dylib" -type f | while read plugin; do
  codesign --force --options runtime --sign "$IDENTITY" "$plugin"
done
```

**Step 5: Main executable and app bundle**
```bash
codesign --force --options runtime --sign "$IDENTITY" "$APP/Contents/MacOS/ScanTailor Spectre"
codesign --force --options runtime --sign "$IDENTITY" "$APP"
```

### If WebEngine is present:

The `QtWebEngineProcess.app` helper inside
`$APP/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/`
must also be signed:

```bash
HELPER="$APP/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app"
if [ -d "$HELPER" ]; then
  codesign --force --options runtime --sign "$IDENTITY" "$HELPER/Contents/MacOS/QtWebEngineProcess"
  codesign --force --options runtime --sign "$IDENTITY" "$HELPER"
  # Re-sign the parent framework and app after touching the helper
  codesign --force --options runtime --sign "$IDENTITY" "$APP/Contents/Frameworks/QtWebEngineCore.framework"
  codesign --force --options runtime --sign "$IDENTITY" "$APP"
fi
```

### Verify

```bash
codesign -vvv "$APP"
```

You should see `valid on disk` and `satisfies its Designated Requirement`.

Note: `codesign --verify --deep --strict` may print `No such file or
directory` on stderr even when the signature is valid — this is a known
codesign bug with spaces in app names. Use `codesign -vvv` instead.

## 3. Notarize

```bash
rm -f app.zip
ditto -c -k --keepParent "$APP" app.zip
xcrun notarytool submit app.zip --keychain-profile "notary" --wait
```

Must use `ditto` — `zip` does not preserve macOS symlinks correctly.

Wait for `status: Accepted`. If it fails, check the log:
```bash
xcrun notarytool log <submission-id> --keychain-profile "notary"
```

## 4. Staple

```bash
xcrun stapler staple "$APP"
```

This embeds the notarization ticket in the app so it works offline.

## 5. Generate README PDF

```bash
cat README.md | \
  sed 's|<img.*/>|<img src="src/resources/scantailor-spectre2.png" width="128" alt="ScanTailor Spectre"/>|' | \
  sed 's|## Quick Start|<div style="page-break-before: always"></div>\n\n## Quick Start {.no-rule}|' | \
  sed 's|## Credits|<div style="page-break-before: always"></div>\n\n## Credits {.no-rule}|' \
  > README_temp.md
pandoc README_temp.md -t html --standalone > README.html
sed -i '' 's|<style>|<style>\n    h2.no-rule { border-bottom: none !important; }|' README.html
/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome \
  --headless --disable-gpu \
  --print-to-pdf="ScanTailor Spectre Readme.pdf" \
  --no-pdf-header-footer README.html
rm README_temp.md
```

## 6. Copy to Applications

```bash
rm -rf /Applications/"ScanTailor Spectre.app"
cp -R "$APP" /Applications/
```

## 7. Create DMG

```bash
VERSION=$(grep 'VERSION "' version.h.in | sed 's/.*"\(.*\)"/\1/')
TIMESTAMP=$(date +%Y%m%d-%H%M)

mkdir -p dmg-staging
cp -R "$APP" dmg-staging/
cp "ScanTailor Spectre Readme.pdf" dmg-staging/

hdiutil create -volname "ScanTailor Spectre" -srcfolder dmg-staging \
    -ov -format UDZO "ScanTailor-Spectre-${VERSION}-${TIMESTAMP}.dmg"

rm -rf dmg-staging
```

## 8. Commit, Tag, Push

```bash
git add <changed files>
git commit -m "Version ${VERSION}: <description>"
git tag "v${VERSION}"
git push origin main --tags
```

## 9. Create GitHub Release

```bash
DMG=$(ls -t ScanTailor-Spectre-${VERSION}-*.dmg | head -1)
gh release create "v${VERSION}" "$DMG" \
  --title "Version ${VERSION}" \
  --notes "Release notes here"
```

## Common Pitfalls

- **Do not modify the app bundle after signing** — copying files in,
  changing resources, etc. invalidates the signature. Do all modifications
  (README PDF into bundle, etc.) BEFORE signing.
- **Sign inner components first, outer last** — framework binaries before
  framework bundles, everything before the app bundle itself.
- **Always use `ditto` for zip** — `zip` breaks framework symlinks.
- **Test the app before releasing** — open a project, click through every
  stage, verify options panels render.
- **WebEngine helper** — `macdeployqt` does not properly bundle
  `QtWebEngineProcess.app`'s dependencies. The helper binary may have
  hardcoded `/opt/homebrew/` paths. See HANDOFF.md for details on fixing
  this if WebEngine is in use.
