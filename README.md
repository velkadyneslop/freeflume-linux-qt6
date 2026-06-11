# FreeFlume

A fast, native desktop **YT client** for Linux — built from the ground up
for big screens, mouse and keyboard. — a small native
C++/Qt6 binary.

![native](https://img.shields.io/badge/native-C%2B%2B%2FQt6-blue)

> FreeFlume began as a desktop reimagining of
> [BravePipe](https://github.com/bravepipeproject/BravePipe) (a
> [NewPipe](https://github.com/TeamNewPipe/NewPipe) fork), and keeps that
> lineage in its name: **Free** (libre — no ads, no account) + **Flume** (the
> channel that carries the flow). It shares no code with the Android app — the
> UI is rebuilt for the desktop and the moving parts are swapped for native
> equivalents.

| Concern    | Android (NewPipe/BravePipe) | FreeFlume (desktop)                |
|------------|-----------------------------|------------------------------------|
| UI         | Android Views/Fragments     | **Qt6 Widgets** (desktop-first)    |
| Extraction | NewPipeExtractor (JVM)      | **yt-dlp** (subprocess)            |
| Playback   | ExoPlayer (Android)         | **libmpv** (embedded, OpenGL)      |
| Storage    | Room/SQLite (Android)       | **Qt Sql / SQLite**                |

## Features

- 🔎 **Search** with thumbnails, pagination, YT-style filters, channels &
  playlists in results, and per-channel search
- 📄 **Detail pane** — description (clickable links), stats, clickable channel
- ▶️ **Embedded player** (libmpv): click-to-seek, hover time preview, quality
  select, captions with full styling, fullscreen with auto-hiding controls —
  keyboard: `Space` `←/→` `↑/↓` `M` `C` `F` `I` `Esc`
- 🧭 **Navigate** — drill into channels/playlists, in-app Back, visit a video's
  channel by clicking its name
- 🕘 **History**, ⭐ **Subscriptions** (channel feeds), 📁 **Playlists** — in SQLite
- 🎨 **Native on every desktop** — Breeze on KDE, gtk3 theme on GNOME; follows
  system light/dark; overridable in Settings

## Run the binary

The prebuilt `freeflume` binary is dynamically linked, so the host needs Qt6,
libmpv, and `yt-dlp` (mpv's package usually provides libmpv):

```bash
# Fedora
sudo dnf install qt6-qtbase-gui mpv yt-dlp
# Arch
sudo pacman -S qt6-base mpv yt-dlp
# Debian / Ubuntu — on 24.04+ the libqt6* names gain a "t64" suffix
sudo apt install libqt6widgets6 libqt6openglwidgets6 libqt6network6 \
                 libqt6sql6-sqlite libqt6dbus6 libmpv2 yt-dlp

./freeflume
```

(If `yt-dlp` is missing, the app shows a popup with the right command for your
distro on launch.)

The Flatpak build bundles Qt, libmpv, and yt-dlp itself — nothing to install.

## Build

See [doc/BUILD.md](doc/BUILD.md). Quick start on Fedora:

```bash
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel mpv-devel yt-dlp mpv
cmake -S . -B build -G Ninja && cmake --build build
./build/freeflume
```

`yt-dlp` and `mpv` are runtime requirements.

## A note

I built FreeFlume for my own use. If you're a dev and want to lift anything from
it, go ahead — take whatever's useful.
