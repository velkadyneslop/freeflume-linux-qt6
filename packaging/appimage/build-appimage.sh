#!/bin/bash
# Runs inside the Ubuntu build container. Builds FreeFlume, bundles Qt + libmpv
# via linuxdeploy, and writes the AppImage to /src/Linux-Release/.
set -e
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=/usr/bin/qmake6
export ARCH=x86_64
export VERSION=1.0.0
export NO_STRIP=1
# Bundle the offscreen plugin (headless testing) and the Wayland platform
# plugins so the app runs as a native Wayland client instead of via XWayland.
# linuxdeploy-plugin-qt pulls in the supporting wayland-* integration plugins.
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland-generic.so;libqwayland-egl.so"

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

# Slim the bundle to fit Codeberg's 100 MB limit: ffmpeg's flite (speech synth,
# ~20 MB of voice data) and sphinx (speech recognition) libs are only used by
# libavfilter filters we never invoke. Ubuntu builds with -z now, so libavfilter
# resolves their symbols at load and we can't just delete them — instead replace
# each lib with a tiny stub that exports the same symbols as no-ops.
LIB="$APPDIR/usr/lib"
cat > /tmp/flite_stub.c <<'CEOF'
long flite_init(){return 0;}  long flite_text_to_wave(){return 0;}  long delete_wave(){return 0;}
void *register_cmu_us_awb(){return 0;}   void unregister_cmu_us_awb(){}
void *register_cmu_us_kal(){return 0;}   void unregister_cmu_us_kal(){}
void *register_cmu_us_kal16(){return 0;} void unregister_cmu_us_kal16(){}
void *register_cmu_us_rms(){return 0;}   void unregister_cmu_us_rms(){}
void *register_cmu_us_slt(){return 0;}   void unregister_cmu_us_slt(){}
CEOF
cat > /tmp/sphinx_stub.c <<'CEOF'
long cmd_ln_free_r(){return 0;}  long cmd_ln_parse_r(){return 0;}
long ps_args(){return 0;}  long ps_default_search_args(){return 0;}
long ps_init(){return 0;}  long ps_free(){return 0;}
long ps_start_utt(){return 0;}  long ps_end_utt(){return 0;}
long ps_process_raw(){return 0;}  long ps_get_hyp(){return 0;}  long ps_get_in_speech(){return 0;}
CEOF
stub_replace() {  # $1 = filename glob, $2 = stub source
    for f in "$LIB"/$1; do
        [ -e "$f" ] || continue
        real="$(readlink -f "$f")"
        son="$(patchelf --print-soname "$real" 2>/dev/null)"
        [ -n "$son" ] || son="$(basename "$real")"
        gcc -shared -fPIC -Wl,-soname,"$son" -o "$real" "$2"
    done
}
stub_replace "libflite*.so*" /tmp/flite_stub.c
stub_replace "libsphinxbase.so*" /tmp/sphinx_stub.c
stub_replace "libpocketsphinx.so*" /tmp/sphinx_stub.c

mkdir -p /src/Linux-Release
appimagetool --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 \
    "$APPDIR" /src/Linux-Release/FreeFlume-1.0.0-x86_64.AppImage
echo "DONE: $(ls -lh /src/Linux-Release/FreeFlume-1.0.0-x86_64.AppImage)"
