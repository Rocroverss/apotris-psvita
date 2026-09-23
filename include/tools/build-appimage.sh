#!/bin/bash
#
# build-appimage.sh - Package Apotris Linux build as an AppImage
#
# This script creates an AppImage by directly concatenating the AppImageKit
# runtime with a squashfs filesystem. No appimagetool or FUSE required.
#
# Prerequisites:
#   1. Build Apotris for Linux first:
#      meson setup -Db_lto=true -Db_lto_mode=thin --buildtype=release build-linux
#      meson compile -C build-linux
#      strip build-linux/Apotris
#   2. Install squashfs-tools (provides mksquashfs)
#
# Usage:
#   ./tools/build-appimage.sh              # use build-linux/ directory
#   ./tools/build-appimage.sh build-foo    # use a different build directory
#

set -euo pipefail

APP_NAME="Apotris"
BUILD_DIR="${1:-build-linux}"
BINARY="$BUILD_DIR/$APP_NAME"

if [ ! -f "$BINARY" ]; then
	echo "Error: $BINARY not found. Build Apotris for Linux first."
	exit 1
fi

# Verify mksquashfs is available
if ! command -v mksquashfs &>/dev/null; then
	echo "Error: mksquashfs not found. Install squashfs-tools."
	exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
APPIMG_ARCH="$(uname -m)"

# ------------------------------------------------------------------
# 1. Download AppImageKit runtime if missing
# ------------------------------------------------------------------
RUNTIME_FILE="$PROJECT_DIR/$BUILD_DIR/runtime-$APPIMG_ARCH"
if [ ! -f "$RUNTIME_FILE" ] || [ ! -s "$RUNTIME_FILE" ]; then
	echo "==> Downloading AppImageKit runtime for $APPIMG_ARCH..."
	wget -q -O "$RUNTIME_FILE" \
		"https://github.com/AppImage/AppImageKit/releases/download/continuous/runtime-$APPIMG_ARCH"
	if [ ! -f "$RUNTIME_FILE" ] || [ ! -s "$RUNTIME_FILE" ]; then
		echo "Error: Failed to download runtime"
		exit 1
	fi
fi

# ------------------------------------------------------------------
# 2. Build the AppDir
# ------------------------------------------------------------------
APPDIR="$PROJECT_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/apotris"

echo "==> Creating AppDir at $APPDIR"

cp "$BINARY" "$APPDIR/usr/bin/$APP_NAME"
cp -r "$PROJECT_DIR/assets" "$APPDIR/usr/share/apotris/assets"
cp -r "$PROJECT_DIR/license" "$APPDIR/usr/share/apotris/license"
find "$APPDIR/usr/share/apotris" -name meson.build -delete

echo "==> Copying icon..."
if command -v convert &>/dev/null; then
	convert "$PROJECT_DIR/sprites/logo512.png" -resize 256x256 \
		"$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png"
else
	cp "$PROJECT_DIR/sprites/logo512.png" \
		"$APPDIR/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png"
fi

echo "==> Installing .desktop file..."
cp "$PROJECT_DIR/dist/Apotris.desktop" "$APPDIR/usr/share/applications/$APP_NAME.desktop"

# Root symlinks (AppImage spec)
ln -s usr/bin/"$APP_NAME" "$APPDIR/AppRun"
ln -s usr/share/applications/"$APP_NAME.desktop" "$APPDIR/$APP_NAME.desktop"
ln -s usr/share/icons/hicolor/256x256/apps/"$APP_NAME.png" "$APPDIR/$APP_NAME.png"

# ------------------------------------------------------------------
# 3. Create squashfs and concatenate with runtime to produce AppImage
# ------------------------------------------------------------------
echo "==> Creating squashfs filesystem..."
SQFS_FILE="$PROJECT_DIR/$BUILD_DIR/_appimage.squashfs"
rm -f "$SQFS_FILE"
mksquashfs "$APPDIR" "$SQFS_FILE" -root-owned -noappend -no-exports -no-xattrs -quiet

echo "==> Assembling AppImage..."
OUTFILE="$PROJECT_DIR/$APP_NAME-$APPIMG_ARCH.AppImage"
cat "$RUNTIME_FILE" "$SQFS_FILE" > "$OUTFILE"
chmod +x "$OUTFILE"

# Embed the AppImage type 2 magic bytes at offset 8
# Type 2 magic: 0x41 0x49 0x02 0x00 at offset 8
printf '\x41\x49\x02\x00' | dd of="$OUTFILE" bs=1 seek=8 count=4 conv=notrunc status=none

rm -f "$SQFS_FILE"

echo ""
echo "==> AppImage created: $OUTFILE"

# ------------------------------------------------------------------
# 4. Validate the AppImage (no FUSE required)
# ------------------------------------------------------------------
echo "==> Validating AppImage..."

# Check file size is reasonable (runtime + squashfs)
file_size=$(stat -c%s "$OUTFILE" 2>/dev/null || stat -f%z "$OUTFILE" 2>/dev/null)
runtime_size=$(stat -c%s "$RUNTIME_FILE" 2>/dev/null || stat -f%z "$RUNTIME_FILE" 2>/dev/null)
echo "    Runtime size: $runtime_size bytes"
echo "    AppImage size: $file_size bytes"

if [ "$file_size" -le "$runtime_size" ]; then
	echo "Error: AppImage is smaller than or equal to the runtime. Something went wrong."
	exit 1
fi

# Verify the squashfs payload can be listed
sqfs_offset=$runtime_size
echo "    Squashfs offset: $sqfs_offset"
tail -c +$((sqfs_offset + 1)) "$OUTFILE" > "$PROJECT_DIR/$BUILD_DIR/_validate.squashfs"
if unsquashfs -l "$PROJECT_DIR/$BUILD_DIR/_validate.squashfs" | grep -q "AppRun"; then
	echo "    AppRun found in squashfs - OK"
else
	rm -f "$PROJECT_DIR/$BUILD_DIR/_validate.squashfs"
	echo "Error: AppRun not found in squashfs payload. AppImage is invalid."
	exit 1
fi
if unsquashfs -l "$PROJECT_DIR/$BUILD_DIR/_validate.squashfs" | grep -q "$APP_NAME.desktop"; then
	echo "    Desktop file found in squashfs - OK"
else
	rm -f "$PROJECT_DIR/$BUILD_DIR/_validate.squashfs"
	echo "Error: Desktop file not found in squashfs payload. AppImage is invalid."
	exit 1
fi
rm -f "$PROJECT_DIR/$BUILD_DIR/_validate.squashfs"

echo "==> Validation passed!"

# ------------------------------------------------------------------
# 5. Cleanup
# ------------------------------------------------------------------
echo "==> Cleaning up AppDir..."
rm -rf "$APPDIR"
echo "==> Done!"
