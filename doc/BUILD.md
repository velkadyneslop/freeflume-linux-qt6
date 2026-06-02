# Building FreeFlume

A fast, native C++/Qt6 desktop YouTube client. No Electron, no JVM.

- **UI:** Qt6 Widgets (+ OpenGLWidgets, Network, Sql)
- **Playback:** libmpv (embedded via the OpenGL render API)
- **Extraction:** `yt-dlp` (invoked as a subprocess)

The app looks native on each desktop: KDE inherits **Breeze**, GNOME uses Qt's
**gtk3 platform theme**. It also follows the system light/dark scheme
(overridable in Settings).

## Source

The Linux source lives on Codeberg: <https://codeberg.org/velkadyne/FreeFlume-linux>

```bash
git clone https://codeberg.org/velkadyne/FreeFlume-linux.git
cd FreeFlume-linux
```

---

## Linux

### Dependencies

**Fedora**
```bash
sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel mpv-devel \
    yt-dlp mpv
# Optional, closer GNOME/Adwaita look:
sudo dnf install qadwaitadecorations-qt6   # if available
```

**Debian / Ubuntu**
```bash
sudo apt install g++ cmake ninja-build pkg-config \
    qt6-base-dev libqt6opengl6-dev libqt6sql6-sqlite \
    libmpv-dev yt-dlp mpv
```

`yt-dlp` and `mpv` are runtime requirements (the binary shells out to `yt-dlp`
and dynamically loads `libmpv`).

### Build & run
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/freeflume                 # open the app
./build/freeflume "lofi hip hop"  # search on launch
./build/freeflume --play <url>    # play a URL directly
./build/freeflume --tab 2         # open a sidebar tab (0..4)
```

### Install
```bash
sudo cmake --install build --prefix /usr/local
install -Dm644 packaging/org.freeflume.Desktop.desktop \
    /usr/local/share/applications/org.freeflume.Desktop.desktop
```

---

## Notes

- Settings persist via `QSettings` (INI under `~/.config`); the SQLite DB and
  config live under `QStandardPaths::AppDataLocation`.
