#!/bin/bash
# Runs inside the Ubuntu build container. Builds FreeFlume, bundles Qt + libmpv
# via linuxdeploy, and writes the AppImage to /src/Linux-Release/.
set -e
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=/usr/bin/qmake6
export ARCH=x86_64
export VERSION=1.0.0
export NO_STRIP=1
# Bundle the offscreen plugin too (headless testing; harmless otherwise).
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"

BUILD=/tmp/build-appimage
APPDIR=/tmp/AppDir
rm -rf "$BUILD" "$APPDIR"

cmake -S /src -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

# Bundle yt-dlp so extraction works without a host install.
mkdir -p "$APPDIR/usr/bin"
wget -q -O "$APPDIR/usr/bin/yt-dlp" \
    https://github.com/yt-dlp/yt-dlp/releases/download/2026.03.17/yt-dlp_linux
chmod +x "$APPDIR/usr/bin/yt-dlp"

# Bundle Breeze icons (the app uses Breeze icon names everywhere). -a keeps the
# theme's symlinks so it stays small. hicolor is the base theme Breeze inherits.
mkdir -p "$APPDIR/usr/share/icons"
for theme in breeze breeze-dark hicolor; do
    [ -d "/usr/share/icons/$theme" ] && cp -a "/usr/share/icons/$theme" "$APPDIR/usr/share/icons/"
done

cd /tmp
# Qt6's xcb plugin dlopens libxcb-cursor, so linuxdeploy won't catch it — bundle
# it explicitly or the app won't start on hosts lacking it.
chmod +x /src/packaging/appimage/AppRun
linuxdeploy --appdir "$APPDIR" --plugin qt \
    --library /usr/lib/x86_64-linux-gnu/libxcb-cursor.so.0 \
    --custom-apprun /src/packaging/appimage/AppRun \
    --desktop-file "$APPDIR/usr/share/applications/org.freeflume.Desktop.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/freeflume.png" \
    --output appimage

mkdir -p /src/Linux-Release
mv /tmp/FreeFlume*.AppImage /src/Linux-Release/FreeFlume-1.0.0-x86_64.AppImage
echo "DONE: $(ls -lh /src/Linux-Release/FreeFlume-1.0.0-x86_64.AppImage)"
