#!/usr/bin/env bash
# Packs the built plugins in build/ into dist/, one archive per platform and format (the names the download links use).
#   tools/package.sh mac|windows|linux
set -euo pipefail
os=$1
art="build/ElixirDesktop_artefacts/Release"
mkdir -p dist
dist=$(cd dist && pwd)
pack() { # format, the file or bundle's parent folder, its name
  local fmt=$1 dir=$2 name=$3
  case $os in
    mac) codesign --force --deep -s - "$dir/$name"; ditto -c -k --sequesterRsrc --keepParent "$dir/$name" "$dist/ElixirDesktop-$os-$fmt.zip" ;;
    windows) (cd "$dir" && 7z a -tzip "$dist/ElixirDesktop-$os-$fmt.zip" "$name" > /dev/null) ;;
    linux) tar -czf "$dist/ElixirDesktop-$os-$fmt.tar.gz" -C "$dir" "$name" ;;
  esac
}
pack vst3 "$art/VST3" "Elixir Desktop.vst3"
pack clap "$art/CLAP" "Elixir Desktop.clap"
case $os in
  mac) pack au "$art/AU" "Elixir Desktop.component"; pack standalone "$art/Standalone" "Elixir Desktop.app" ;;
  windows) pack standalone "$art/Standalone" "Elixir Desktop.exe" ;;
  linux) pack lv2 "$art/LV2" "Elixir Desktop.lv2"; pack standalone "$art/Standalone" "Elixir Desktop" ;;
esac
ls -l "$dist"
