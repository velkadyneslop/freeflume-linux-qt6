#!/bin/bash
# Runs inside the Ubuntu build container. Builds FreeFlume, bundles Qt + libmpv
# via linuxdeploy, and writes the AppImage to /src/Linux-Release/.
set -e
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=/usr/bin/qmake6
export ARCH=x86_64
export VERSION="${VERSION:-1.0.4}"
export NO_STRIP=1
# Bundle the offscreen plugin too (headless testing; harmless otherwise).
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"

BUILD=/tmp/build-appimage
APPDIR=/tmp/AppDir
rm -rf "$BUILD" "$APPDIR"

cmake -S /src -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

# Note: yt-dlp is NOT bundled. GitHub's 2 GB asset limit no longer forces this
# (Codeberg's old 100 MB cap did), but a host yt-dlp stays current on its own —
# a bundled copy would go stale and break extraction within weeks. The app/mpv
# find it on the host PATH; the Flatpak is the fully self-contained option.

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

# The Qt6 Breeze widget style (from KDE neon) — the AppImage's default look, to
# match the KDE/Flatpak build. Passed to linuxdeploy via --library so it pulls
# Breeze's KF6 dependencies into usr/lib using the proper exclude list; we then
# relocate the plugin itself into the styles dir below.
BREEZE_STYLE="$QT_PLUGINS_DIR/styles/breeze6.so"

# Native Wayland plugins. linuxdeploy-plugin-qt does NOT deploy the Wayland
# stack, so pass each piece via --library (which pulls libQt6WaylandClient and
# friends into usr/lib) and relocate them into the right plugin subdirs below:
#   platforms/libqwayland.so .......................... the Wayland platform plugin
#   wayland-shell-integration/libxdg-shell.so ......... xdg-shell (window creation, KWin)
#   wayland-graphics-integration-client/libqt-plugin-wayland-egl.so  EGL/GL (mpv widget)
#   wayland-decoration-client/libbradient.so .......... client-side decorations fallback
# The platform plugin dlopens the shell/EGL/decoration plugins at runtime, so
# they must all be present or native Wayland silently fails to xcb.
WL_PLATFORM="$QT_PLUGINS_DIR/platforms/libqwayland.so"
WL_SHELL="$QT_PLUGINS_DIR/wayland-shell-integration/libxdg-shell.so"
WL_EGL="$QT_PLUGINS_DIR/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so"
WL_DECO="$QT_PLUGINS_DIR/wayland-decoration-client/libbradient.so"

# KDE platform theme (plasma-integration): lets the AppImage read the host's
# kdeglobals color scheme on KDE so it matches the native build. Pulls a KF6/KIO
# closure via --library; AppRun only activates it (QT_QPA_PLATFORMTHEME=kde) on
# KDE sessions, so non-KDE hosts are unaffected.
KDE_PLATFORM_THEME="$QT_PLUGINS_DIR/platformthemes/KDEPlasmaPlatformTheme6.so"

cd /tmp
# Qt6's xcb plugin dlopens libxcb-cursor, so linuxdeploy won't catch it — bundle
# it explicitly or the app won't start on hosts lacking it.
chmod +x /src/packaging/appimage/AppRun
# Bundle (no --output); we package separately with xz for a smaller file.
linuxdeploy --appdir "$APPDIR" --plugin qt \
    --library /usr/lib/x86_64-linux-gnu/libxcb-cursor.so.0 \
    --library "$BREEZE_STYLE" \
    --library "$WL_PLATFORM" \
    --library "$WL_SHELL" \
    --library "$WL_EGL" \
    --library "$WL_DECO" \
    --library "$KDE_PLATFORM_THEME" \
    --custom-apprun /src/packaging/appimage/AppRun \
    --desktop-file "$APPDIR/usr/share/applications/org.freeflume.Desktop.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/freeflume.png"

# --library put breeze6.so (and its KF6 deps) in usr/lib. Move the style plugin
# into the Qt styles dir where QStyleFactory looks, and repoint its RUNPATH at
# the bundled libs. Its dependencies stay in usr/lib, already rpath-patched.
mkdir -p "$APPDIR/usr/plugins/styles"
mv "$APPDIR/usr/lib/breeze6.so" "$APPDIR/usr/plugins/styles/breeze6.so"
patchelf --set-rpath '$ORIGIN/../../lib' "$APPDIR/usr/plugins/styles/breeze6.so"

# Same for the Wayland plugins: relocate each from usr/lib (where --library put
# them) into its Qt plugin subdir, with RUNPATH pointing at the bundled libs.
mkdir -p "$APPDIR/usr/plugins/platforms" \
         "$APPDIR/usr/plugins/wayland-shell-integration" \
         "$APPDIR/usr/plugins/wayland-graphics-integration-client" \
         "$APPDIR/usr/plugins/wayland-decoration-client"
mv "$APPDIR/usr/lib/libqwayland.so"              "$APPDIR/usr/plugins/platforms/"
mv "$APPDIR/usr/lib/libxdg-shell.so"             "$APPDIR/usr/plugins/wayland-shell-integration/"
mv "$APPDIR/usr/lib/libqt-plugin-wayland-egl.so" "$APPDIR/usr/plugins/wayland-graphics-integration-client/"
mv "$APPDIR/usr/lib/libbradient.so"              "$APPDIR/usr/plugins/wayland-decoration-client/"
for p in platforms/libqwayland.so \
         wayland-shell-integration/libxdg-shell.so \
         wayland-graphics-integration-client/libqt-plugin-wayland-egl.so \
         wayland-decoration-client/libbradient.so; do
    patchelf --set-rpath '$ORIGIN/../../lib' "$APPDIR/usr/plugins/$p"
done

# Relocate the KDE platform theme into platformthemes/ (its KF6/KIO deps stay in
# usr/lib). Activated by AppRun via QT_QPA_PLATFORMTHEME=kde on KDE sessions.
mkdir -p "$APPDIR/usr/plugins/platformthemes"
mv "$APPDIR/usr/lib/KDEPlasmaPlatformTheme6.so" "$APPDIR/usr/plugins/platformthemes/"
patchelf --set-rpath '$ORIGIN/../../lib' "$APPDIR/usr/plugins/platformthemes/KDEPlasmaPlatformTheme6.so"

# Slim the bundle (smaller download + smaller self-update deltas): ffmpeg's flite (speech synth,
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

# Bundle the self-updater LAST (after linuxdeploy, so it doesn't try to patch
# the inner AppImage's RUNPATH). `FreeFlume.AppImage --update` runs it; it's an
# AppImage itself, launched with extract-and-run by AppRun so it needs no FUSE.
cp /opt/appimageupdatetool-x86_64.AppImage "$APPDIR/usr/bin/appimageupdatetool.AppImage"
chmod +x "$APPDIR/usr/bin/appimageupdatetool.AppImage"

mkdir -p /src/Linux-Release
OUT_DIR="/src/Linux-Release"
OUT_NAME="FreeFlume-${VERSION}-x86_64.AppImage"
OUT="$OUT_DIR/$OUT_NAME"

# -u embeds the update-information string and makes appimagetool emit the zsync
# delta control file. The gh-releases-zsync transport points the updater at the
# latest GitHub release; the glob is version-independent so it keeps matching
# across releases. BOTH the .AppImage AND the .zsync must be uploaded to every
# GitHub release for self-update to resolve.
UPDATE_INFO='gh-releases-zsync|velkadyneslop|freeflume-linux-qt6|latest|FreeFlume-*-x86_64.AppImage.zsync'

# Optional GPG signing (opt-in): export SIGN_KEY=<key-id-or-email> to sign the
# AppImage so the updater/users can verify authenticity. Builds without it still
# succeed, just unsigned.
SIGN_ARGS=()
[ -n "${SIGN_KEY:-}" ] && SIGN_ARGS=(--sign --sign-key "${SIGN_KEY}")

# Run from the output dir: appimagetool's zsyncmake writes the .zsync to the
# current working directory, so cwd must be where we want it to land (not the
# container's /tmp, which vanishes with --rm).
cd "$OUT_DIR"
appimagetool --comp zstd --mksquashfs-opt -Xcompression-level --mksquashfs-opt 22 \
    -u "${UPDATE_INFO}" "${SIGN_ARGS[@]}" \
    "$APPDIR" "$OUT_NAME"
echo "DONE: $(ls -lh "$OUT")"
if [ -f "${OUT}.zsync" ]; then
    echo "zsync: $(ls -lh "${OUT}.zsync")  <-- upload alongside the .AppImage"
else
    echo "ERROR: ${OUT}.zsync was not produced — self-update will not work." >&2
    exit 1
fi
