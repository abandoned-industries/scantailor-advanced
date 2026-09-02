# Building ScanTailor Spectre for macOS

ScanTailor Spectre is a macOS-only fork of ScanTailor Advanced (macOS 15+,
Apple Silicon). This is a **build** guide only.

> **Signing, notarization, stapling, DMG/ZIP release artifacts, and
> GitHub publication are covered exclusively by `SIGNING.md` at the repo
> root.** That document is authoritative; nothing in this file or in
> `create-dmg.sh` replaces it.

## Source

Development happens in the **private checkout** (remote `origin`, a
private forge). The GitHub repository is a sanitized
public snapshot, not the development history — do not build releases from it
and never push private history to it (see `SIGNING.md`).

## Prerequisites

Xcode command line tools and Homebrew, then:

```bash
xcode-select --install
brew install cmake qt6 boost libtiff libpng jpeg leptonica
```

Qt 6 must include the WebEngine/WebChannel modules; the Homebrew `qt6`
metapackage provides them.

## Configure

The canonical build directory is `build/` at the repo root — always. Do not
create ad-hoc build directories. Leptonica is found via pkg-config, so the
configure step needs `PKG_CONFIG_PATH`:

```bash
cd <repo root>
PKG_CONFIG_PATH=/opt/homebrew/lib/pkgconfig cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$(brew --prefix qt6) \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
```

Distribution builds additionally set `-DPORTABLE_VERSION=OFF`
(see `SIGNING.md`).

## Build targets

Two targets, two purposes:

- **`scantailor`** — fast incremental app build for day-to-day development:

  ```bash
  cmake --build build --target scantailor -j$(sysctl -n hw.ncpu)
  ```

- **`scantailor_bundle`** — the explicit bundle/deploy step: rebuilds the
  app, runs `macdeployqt`, fixes bundled library paths, and ad-hoc signs the
  finished bundle. Run it only when preparing a distributable bundle, not on
  every incremental build:

  ```bash
  cmake --build build --target scantailor_bundle
  ```

Tests:

```bash
cd build && ctest --output-on-failure
```

## Hazard: never run macdeployqt by hand on the app inside build/

Do **not** invoke `macdeployqt` manually on
`build/ScanTailor Spectre.app`. A hand-deployed bundle inside `build/`
breaks every later incremental dev build: the fresh executable links
Homebrew Qt while the stale bundled `qt.conf`/`PlugIns` force the old cocoa
plugin, and the app aborts at launch (`qt.qpa.plugin` cocoa error). Deploy
only via the `scantailor_bundle` target, or onto a *copy* staged outside
`build/`.

Cure if it happens — remove the stale deployment from the build/ app:

```bash
cd "build/ScanTailor Spectre.app"
rm -rf Contents/Frameworks Contents/PlugIns Contents/Resources/qt.conf Contents/Resources/qml
```

## Unsigned DMG for local testing

`packaging/macos/create-dmg.sh` stages the built app (plus the bundled
Zotero companion plugin) into an **unsigned** DMG for local testing. It
performs no signing or notarization. For any artifact that leaves this
machine, follow `SIGNING.md` end to end instead: Developer ID component
signing, notarization with the machine-wide `notary` keychain profile,
stapling, identically timestamped ZIP + DMG, and mounted-DMG verification.

## Troubleshooting

- **Qt not found**: check `brew --prefix qt6` and re-run configure with
  `-DCMAKE_PREFIX_PATH=$(brew --prefix qt6)`.
- **Leptonica not found**: the configure step was run without
  `PKG_CONFIG_PATH=/opt/homebrew/lib/pkgconfig`.
- **App aborts at launch with a cocoa plugin error**: see the macdeployqt
  hazard above.
