#!/bin/bash
# Install the newest Elixir Desktop on a Mac: VST3, Audio Unit, CLAP and the standalone app.
#
#   curl -fsSL https://raw.githubusercontent.com/johnnyhawk02/elixir-desktop-vst/main/install-mac.sh | bash
#
# Files fetched with curl get no quarantine flag, so macOS doesn't block the unsigned plugin.
# Nothing needs sudo: plugins go in your own Library, the app in /Applications
# (or ~/Applications if /Applications isn't writable).
set -euo pipefail

BASE="https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download"
PLUGINS="$HOME/Library/Audio/Plug-Ins"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ "$(uname)" != "Darwin" ]; then
  echo "This installer is for macOS. Other systems: https://github.com/johnnyhawk02/elixir-desktop-vst/blob/main/docs/DOWNLOADS.md" >&2
  exit 1
fi

APPS="/Applications"
[ -w "$APPS" ] || { APPS="$HOME/Applications"; mkdir -p "$APPS"; }

install_one() { # zip-suffix bundle-name destination-folder
  local zip="ElixirDesktop-mac-$1.zip" bundle="$2" dest="$3"
  echo "Downloading $zip"
  curl -fL --progress-bar -o "$TMP/$zip" "$BASE/$zip"
  mkdir -p "$TMP/$1" "$dest"
  ditto -x -k "$TMP/$zip" "$TMP/$1"
  rm -rf "$dest/$bundle"
  mv "$TMP/$1/$bundle" "$dest/"
  xattr -dr com.apple.quarantine "$dest/$bundle" 2>/dev/null || true
  echo "  installed $dest/$bundle"
}

install_one vst3       "Elixir Desktop.vst3"      "$PLUGINS/VST3"
install_one au         "Elixir Desktop.component" "$PLUGINS/Components"
install_one clap       "Elixir Desktop.clap"      "$PLUGINS/CLAP"
install_one standalone "Elixir Desktop.app"       "$APPS"

# Make Logic and GarageBand notice the new Audio Unit.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Done. Rescan plugins in your music software, or open Elixir Desktop from $APPS."
