#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build a Linux AppImage of the SDL3 front end.
#
# Why the SDL front end and not the GTK one: an AppImage of the GTK build needs
# linuxdeploy-plugin-gtk to bundle GTK, its schemas, gdk-pixbuf loaders and the
# icon themes, which makes it ~80 MB.  The SDL build has no toolkit runtime to
# carry: SDL3 plus fftw3 and liblo, ~10 MB, and it is the front end that also
# runs on Windows and macOS.
#
# Requirements: make, gcc/g++, pkg-config, curl (and the SDL3 development
# package).  linuxdeploy is downloaded once into .cache/ and reused.
#
# Usage: ./packaging/build-appimage.sh [--no-bundle]
#        --no-bundle keeps the AppDir without producing the AppImage (useful in
#        containers or when FUSE is not available).

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

APP_NAME=spectrum-analyzer
APPDIR="$ROOT/$APP_NAME.AppDir"
CACHE="$ROOT/.cache"
LINUXDEPLOY="$CACHE/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"

BUNDLE=yes
[ "${1:-}" = "--no-bundle" ] && BUNDLE=no

echo "==> building the SDL3 front end"
make sdl

VERSION=${VERSION:-$(sed -n 's/^#define SPECTRUM_ANALYZER_VERSION "\(.*\)"$/\1/p' "$ROOT/version.h")}
[ -n "$VERSION" ] || VERSION=0.0.0
echo "==> version $VERSION"

echo "==> staging $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" \
	"$APPDIR/usr/share/applications" \
	"$APPDIR/usr/share/icons/hicolor/scalable/apps" \
	"$APPDIR/usr/share/doc/$APP_NAME" \
	"$APPDIR/usr/share/licenses/$APP_NAME"

# The AppImage ships the portable front end under the plain name users expect.
install -m755 spectrum-analyzer-sdl "$APPDIR/usr/bin/$APP_NAME"
install -m644 spectrum-analyzer-icon.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_NAME.svg"
install -m644 packaging/$APP_NAME.desktop "$APPDIR/usr/share/applications/$APP_NAME.desktop"
install -m644 LICENSE "$APPDIR/usr/share/licenses/$APP_NAME/LICENSE"
install -m644 vendor/miniaudio-LICENSE "$APPDIR/usr/share/licenses/$APP_NAME/miniaudio-LICENSE"
install -m644 README.md "$APPDIR/usr/share/doc/$APP_NAME/README.md"

# Top-level icon and desktop file for tools that look for them there.
cp "$APPDIR/usr/share/icons/hicolor/scalable/apps/$APP_NAME.svg" "$APPDIR/$APP_NAME.svg"
cp "$APPDIR/usr/share/applications/$APP_NAME.desktop" "$APPDIR/$APP_NAME.desktop"

# Tell the launcher where the binary actually is inside the bundle.
sed -i 's|^Exec=.*|Exec=spectrum-analyzer|' "$APPDIR/$APP_NAME.desktop"

if [ ! -x "$LINUXDEPLOY" ]; then
	echo "==> downloading linuxdeploy"
	mkdir -p "$CACHE"
	curl -fsSL -o "$LINUXDEPLOY" "$LINUXDEPLOY_URL"
	chmod +x "$LINUXDEPLOY"
fi

echo "==> collecting libraries"
# --appimage-extract-and-run works without FUSE (containers, CI).
"$LINUXDEPLOY" --appimage-extract-and-run \
	--appdir "$APPDIR" \
	--desktop-file "$APPDIR/$APP_NAME.desktop" \
	--icon-file "$APPDIR/$APP_NAME.svg"

if [ "$BUNDLE" = no ]; then
	echo "==> AppDir ready: $APPDIR (AppImage not requested)"
	exit 0
fi

echo "==> producing the AppImage"
"$LINUXDEPLOY" --appimage-extract-and-run \
	--appdir "$APPDIR" \
	--output appimage

ls -la "$ROOT"/*.AppImage
