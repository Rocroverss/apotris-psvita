#!/bin/bash
#
# install.sh - Install Apotris to ~/.local/ (no sudo required)
#
# Run from the extracted Apotris zip directory.
# Installs the game binary, assets, launcher, desktop entry, and icon
# entirely under $HOME/.local/.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

BOLD="\033[1m"
GREEN="\033[32m"
YELLOW="\033[33m"
RED="\033[31m"
RESET="\033[0m"

info() { echo -e "${GREEN}==>${RESET} ${BOLD}$*${RESET}"; }
warn() { echo -e "${YELLOW}⚠${RESET}  $*"; }
error() { echo -e "${RED}✗${RESET}  $*"; }

# ---------------------------------------------------------------------------
# Resolve install paths
# ---------------------------------------------------------------------------
LOCAL_BIN="$HOME/.local/bin"
LOCAL_SHARE="$HOME/.local/share"
APOTRIS_DATA="$LOCAL_SHARE/apotris"
APPS_DIR="$LOCAL_SHARE/applications"
ICON_DIR="$LOCAL_SHARE/icons/hicolor/512x512/apps"
DESKTOP_FILE="$APPS_DIR/Apotris.desktop"
LAUNCHER="$LOCAL_BIN/apotris"

# ---------------------------------------------------------------------------
# Sanity checks
# ---------------------------------------------------------------------------
if [ ! -f "$SCRIPT_DIR/Apotris" ]; then
	error "Apotris binary not found in $SCRIPT_DIR"
	error "Run this script from the extracted Apotris zip directory."
	exit 1
fi

if [ ! -f "$SCRIPT_DIR/Apotris.desktop" ]; then
	error "Apotris.desktop not found in $SCRIPT_DIR"
	exit 1
fi

if [ ! -f "$SCRIPT_DIR/Apotris.png" ]; then
	error "Apotris.png (icon) not found in $SCRIPT_DIR"
	exit 1
fi

# ---------------------------------------------------------------------------
# Already installed? Offer to overwrite
# ---------------------------------------------------------------------------
if [ -d "$APOTRIS_DATA" ] || [ -f "$LAUNCHER" ] || [ -f "$DESKTOP_FILE" ]; then
	echo ""
	warn "Apotris appears to be already installed."
	echo "    Data dir : $APOTRIS_DATA"
	echo "    Launcher : $LAUNCHER"
	echo "    Desktop  : $DESKTOP_FILE"
	echo ""
	read -rp "Overwrite existing installation? [Y/n] " answer
	if [ "$answer" = "n" ] || [ "$answer" = "N" ]; then
		info "Aborting."
		exit 0
	fi
	rm -rf "$APOTRIS_DATA"
	rm -f "$LAUNCHER"
	rm -f "$DESKTOP_FILE"
	rm -f "$ICON_DIR/Apotris.png"
fi

# ---------------------------------------------------------------------------
# Create directories
# ---------------------------------------------------------------------------
info "Creating directories..."
mkdir -p "$LOCAL_BIN"
mkdir -p "$APOTRIS_DATA"
mkdir -p "$APPS_DIR"
mkdir -p "$ICON_DIR"

# ---------------------------------------------------------------------------
# Install binary + data
# ---------------------------------------------------------------------------
info "Installing binary and data to $APOTRIS_DATA ..."
cp "$SCRIPT_DIR/Apotris" "$APOTRIS_DATA/Apotris"
chmod +x "$APOTRIS_DATA/Apotris"

if [ -d "$SCRIPT_DIR/assets" ]; then
	cp -r "$SCRIPT_DIR/assets" "$APOTRIS_DATA/assets"
	# Remove meson build remnants if present
	rm -f "$APOTRIS_DATA/assets/meson.build"
fi

if [ -d "$SCRIPT_DIR/license" ]; then
	cp -r "$SCRIPT_DIR/license" "$APOTRIS_DATA/license"
fi

# ---------------------------------------------------------------------------
# Create launcher script
# ---------------------------------------------------------------------------
info "Creating launcher at $LAUNCHER ..."
cat >"$LAUNCHER" <<'LAUNCHER_EOF'
#!/bin/bash
cd "$HOME/.local/share/apotris"
exec ./Apotris "$@"
LAUNCHER_EOF
chmod +x "$LAUNCHER"

# ---------------------------------------------------------------------------
# Install desktop entry (patched with proper Exec path)
# ---------------------------------------------------------------------------
info "Installing desktop entry to $DESKTOP_FILE ..."
cp "$SCRIPT_DIR/Apotris.desktop" "$DESKTOP_FILE"
chmod +x "$DESKTOP_FILE"

# ---------------------------------------------------------------------------
# Install icon
# ---------------------------------------------------------------------------
info "Installing icon to $ICON_DIR ..."
cp "$SCRIPT_DIR/Apotris.png" "$ICON_DIR/Apotris.png"

# ---------------------------------------------------------------------------
# Refresh caches
# ---------------------------------------------------------------------------
if command -v update-desktop-database &>/dev/null; then
	update-desktop-database "$APPS_DIR" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache &>/dev/null; then
	gtk-update-icon-cache "$LOCAL_SHARE/icons/hicolor/" 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# PATH check
# ---------------------------------------------------------------------------
echo ""
info "Installation complete!"

if [[ ":$PATH:" != *":$LOCAL_BIN:"* ]]; then
	warn "$LOCAL_BIN is not in your PATH."
	echo "    Add this to your ~/.bashrc or ~/.profile:"
	echo ""
	echo -e "        ${BOLD}export PATH=\"\$HOME/.local/bin:\$PATH\"${RESET}"
	echo ""
fi

echo "You can now launch Apotris from your application menu,"
echo "or by running:  ${BOLD}apotris${RESET}"
echo ""
