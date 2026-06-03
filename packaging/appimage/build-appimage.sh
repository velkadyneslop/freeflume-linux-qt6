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

# Note: yt-dlp is NOT bundled (its 35 MB binary would push the AppImage over
# Codeberg's 100 MB release-asset limit, and a host yt-dlp stays up to date).
# The app/mpv find it on the host PATH; the Flatpak is the fully self-contained
# option.

# Bundle Breeze icons (the app uses Breeze icon names everywhere). -a keeps the
# theme's symlinks so it stays small. hicolor is the base theme Breeze inherits.
mkdir -p "$APPDIR/usr/share/icons"
for theme in breeze breeze-dark hicolor; do
    [ -d "/usr/share/icons/$theme" ] && cp -a "/usr/share/icons/$theme" "$APPDIR/usr/share/icons/"
done

# linuxdeploy-plugin-qt doesn't bundle the iconengines category, but the SVG
# icon engine is required to render Breeze's SVG icons. Drop it in by hand
# (linuxdeploy then patches its RUNPATH; libQt6Svg is already bundled).
QT_PLUGINS_DIR="$(qmake6 -query QT_INSTALL_PLUGINS)"
mkdir -p "$APPDIR/usr/plugins/iconengines"
cp "$QT_PLUGINS_DIR/iconengines/libqsvgicon.so" "$APPDIR/usr/plugins/iconengines/"
# linuxdeploy won't patch a file we add, so point it at the bundled libs itself.
patchelf --set-rpath '$ORIGIN/../../lib' "$APPDIR/usr/plugins/iconengines/libqsvgicon.so"

cd /tmp
# Qt6's xcb plugin dlopens libxcb-cursor, so linuxdeploy won't catch it — bundle
# it explicitly or the app won't start on hosts lacking it.
chmod +x /src/packaging/appimage/AppRun
# Bundle (no --output); we package separately with xz for a smaller file.
linuxdeploy --appdir "$APPDIR" --plugin qt \
    --library /usr/lib/x86_64-linux-gnu/libxcb-cursor.so.0 \
    --custom-apprun /src/packaging/appimage/AppRun \
    --desktop-file "$APPDIR/usr/share/applications/org.freeflume.Desktop.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/freeflume.png"

# Trim ffmpeg speech synthesis (flite) + recognition (sphinx) libs — only used
# by libavfilter filters we never invoke — to fit Codeberg's 100 MB limit.
# They're NEEDED but lazy-bound, so drop the NEEDED entries then delete the libs.
AVF="$(readlink -f "$APPDIR/usr/lib/libavfilter.so.9")"
for f in "$APPDIR"/usr/lib/libflite*.so* "$APPDIR"/usr/lib/libsphinxbase*.so* \
         "$APPDIR"/usr/lib/libpocketsphinx*.so*; do
    [ -e "$f" ] || continue
    patchelf --remove-needed "$(basename "$f")" "$AVF" 2>/dev/null || true
    rm -f "$f"
done

mkdir -p /src/Linux-Release
appimagetool --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 \
    "$APPDIR" /src/Linux-Release/FreeFlume-1.0.0-x86_64.AppImage
echo "DONE: $(ls -lh /src/Linux-Release/FreeFlume-1.0.0-x86_64.AppImage)"
