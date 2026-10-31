#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bundle the SDL3 front end into a macOS .app and zip it.
#
# Bundled dylibs are copied into Contents/Frameworks and rewritten to @rpath so
# the result runs on a machine that does not have Homebrew.  dylibbundler does
# that rewriting; without it the .app still builds but depends on /opt/homebrew.
#
# Usage: ./packaging/build-macos-app.sh
# Note: for distribution outside your own machine the bundle must be signed and
# notarized (codesign + notarytool, Apple Developer account required).

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

VERSION=${VERSION:-$(sed -n 's/^#define SPECTRUM_ANALYZER_VERSION "\(.*\)"$/\1/p' "$ROOT/version.h")}
[ -n "$VERSION" ] || VERSION=0.0.0

APP_NAME="Spectrum Analyzer"
APP="$ROOT/$APP_NAME.app"
BIN=spectrum-analyzer-sdl

echo "==> building the SDL3 front end"
make sdl

echo "==> staging $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"

install -m755 "$BIN" "$APP/Contents/MacOS/spectrum-analyzer"

# The PNG is what we have; macOS converts it to .icns at build time when
# iconutil is available.
cp spectrum-analyzer-status-icon.png "$APP/Contents/Resources/"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>Spectrum Analyzer</string>
	<key>CFBundleDisplayName</key><string>Spectrum Analyzer</string>
	<key>CFBundleExecutable</key><string>spectrum-analyzer</string>
	<key>CFBundleIdentifier</key><string>org.spectrum-analyzer.SpectrumAnalyzer</string>
	<key>CFBundleShortVersionString</key><string>${VERSION}</string>
	<key>CFBundleVersion</key><string>${VERSION}</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>NSMicrophoneUsageDescription</key>
	<string>Spectrum Analyzer reads audio from the selected input device to draw the spectrum.</string>
</dict>
</plist>
PLIST

mkdir -p "$APP/Contents/Resources/licenses"
cp LICENSE "$APP/Contents/Resources/licenses/LICENSE"
cp vendor/miniaudio-LICENSE "$APP/Contents/Resources/licenses/miniaudio-LICENSE"

if command -v dylibbundler >/dev/null 2>&1; then
	echo "==> bundling dylibs"
	dylibbundler -of -cd -b -x "$APP/Contents/MacOS/spectrum-analyzer" \
		-d "$APP/Contents/Frameworks" -p '@executable_path/../Frameworks/' >/dev/null
else
	echo "!!  dylibbundler not found: the .app will depend on Homebrew libraries."
	echo "!!  install it with: brew install dylibbundler"
fi

echo "==> zipping"
ZIP="Spectrum-Analyzer-macos-$(uname -m).zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
ls -la "$ZIP"
