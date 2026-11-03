# Download Elixir Desktop

A six voice drum machine built on the LXR-02 engine, with step sequencers on every control, for your music software. Free and open source (GPL-3.0).

These links always give the newest version.

| Format | Mac (Apple Silicon + Intel, macOS 11+) | Windows (10/11, 64-bit) | Linux (x86-64) |
|---|---|---|---|
| VST3 | [Mac VST3](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-mac-vst3.zip) | [Windows VST3](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-windows-vst3.zip) | [Linux VST3](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-linux-vst3.tar.gz) |
| Audio Unit | [Mac AU](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-mac-au.zip) | | |
| CLAP | [Mac CLAP](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-mac-clap.zip) | [Windows CLAP](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-windows-clap.zip) | [Linux CLAP](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-linux-clap.tar.gz) |
| LV2 | | | [Linux LV2](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-linux-lv2.tar.gz) |
| Standalone app | [Mac app](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-mac-standalone.zip) | [Windows app](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-windows-standalone.zip) | [Linux app](https://github.com/johnnyhawk02/elixir-desktop-vst/releases/latest/download/ElixirDesktop-linux-standalone.tar.gz) |

All versions: https://github.com/johnnyhawk02/elixir-desktop-vst/releases

## Install on Mac

The quickest way: paste this into Terminal. It installs the newest VST3, Audio Unit, CLAP and app, and macOS won't block them.

```
curl -fsSL https://raw.githubusercontent.com/johnnyhawk02/elixir-desktop-vst/main/install-mac.sh | bash
```

Only want one format? Add it at the end after `bash -s --` (any of `vst3`, `au`, `clap`, `app`):

```
curl -fsSL https://raw.githubusercontent.com/johnnyhawk02/elixir-desktop-vst/main/install-mac.sh | bash -s -- vst3
```

Or install by hand: unzip, then move the file to:

| Format | Folder |
|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3` |
| Audio Unit | `~/Library/Audio/Plug-Ins/Components` |
| CLAP | `~/Library/Audio/Plug-Ins/CLAP` |
| Standalone | `Applications` |

To open the Library folder, press Cmd+Shift+G in Finder and paste the path.

The plugin isn't signed by Apple, so the Mac blocks it at first. Open Terminal and run the line for your format:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Elixir Desktop.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Elixir Desktop.component"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/"Elixir Desktop.clap"
xattr -dr com.apple.quarantine /Applications/"Elixir Desktop.app"
```

## Install on Windows

Unzip, then move the file to:

| Format | Folder |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3` |
| CLAP | `C:\Program Files\Common Files\CLAP` |
| Standalone | anywhere you like |

The screen uses Microsoft WebView2, which comes with Windows 11 and up-to-date Windows 10. If the plugin's window stays blank, install the [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/). If Windows warns about the standalone app, choose More info, then Run anyway.

## Install on Linux

Unpack with `tar -xzf`, then move the file to:

| Format | Folder |
|---|---|
| VST3 | `~/.vst3` |
| CLAP | `~/.clap` |
| LV2 | `~/.lv2` |
| Standalone | anywhere you like |

The screen needs WebKitGTK. On Ubuntu or Debian: `sudo apt install libwebkit2gtk-4.1-0 libgtk-3-0`.

## Then

Rescan plugins in your music software and load **Elixir Desktop** (by Johnny) on an instrument track. In Ableton Live: Settings, Plug-Ins, Rescan. The screen's web fonts need an internet connection; offline it falls back to a system font.
