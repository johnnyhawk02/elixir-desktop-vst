#!/bin/bash
# Install the newest Elixir Desktop on a Mac: VST3, Audio Unit, CLAP and the standalone app.
#
#   curl -fsSL https://raw.githubusercontent.com/johnnyhawk02/elixir-desktop-vst/main/install-mac.sh | bash
#
# To install only some formats, name them after "bash -s --" (any of vst3, au, clap, app):
#
#   curl -fsSL https://raw.githubusercontent.com/johnnyhawk02/elixir-desktop-vst/main/install-mac.sh | bash -s -- vst3
#   curl -fsSL https://raw.githubusercontent.com/johnnyhawk02/elixir-desktop-vst/main/install-mac.sh | bash -s -- au clap
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

[ $# -gt 0 ] || set -- vst3 au clap app   # no arguments: install everything
for f in "$@"; do
  case "$(echo "$f" | tr '[:upper:]' '[:lower:]')" in
    vst3|vst)                 install_one vst3       "Elixir Desktop.vst3"      "$PLUGINS/VST3" ;;
    au|component|audiounit)   install_one au         "Elixir Desktop.component" "$PLUGINS/Components"
                              killall -9 AudioComponentRegistrar 2>/dev/null || true ;; # so Logic and GarageBand see it
    clap)                     install_one clap       "Elixir Desktop.clap"      "$PLUGINS/CLAP" ;;
    app|standalone)           install_one standalone "Elixir Desktop.app"       "$APPS" ;;
    *) echo "Unknown format: $f (use vst3, au, clap or app)" >&2; exit 1 ;;
  esac
done

echo "Done. Rescan plugins in your music software, or open Elixir Desktop from $APPS."
