#!/bin/bash
# Focus AppImage rebuild from /app (rebranded Okular).
# Based on build5-inside.sh, updated for Focus names + crash fixes:
#  - binary /app/bin/focus, part focuspart.so, generators focus_generators/
#  - desktop org.kde.focus.desktop, icon focus.png (transparent reticle)
#  - patchelf repair step (fixes patchelf-0.15 corruptions that segfault)
#  - drop DT_NEEDED libcrypt.so.2 from libsasl2 (unused link; Fedora
#    libcrypt segfaults at init on Ubuntu hosts)
# Invoke from host:
#   docker run --rm -v ~/focus-demo/app:/app:ro -v ~/focus-demo/packaging:/work \
#     -w /tmp -e ARCH=x86_64 okular-custom:lib1 bash /work/build-focus-inside.sh
set -ex
export PATH=/work/plugin-extracted/usr/bin:/work/linuxdeploy-extracted/usr/bin:/work/appimagetool-extracted/usr/bin:$PATH
export QMAKE=/usr/bin/qmake6
export NO_STRIP=1
export LINUXDEPLOY_NO_STRIP=1
export ARCH=x86_64
APPDIR=/work/AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr"
cp -a /app/* "$APPDIR/usr/"
# drop stale pre-rename plugins if present
rm -rf "$APPDIR/usr/lib64/plugins/okular_generators"
cp /app/share/applications/org.kde.focus.desktop "$APPDIR/"
cp /app/share/icons/hicolor/128x128/apps/focus.png "$APPDIR/focus.png"
cd /tmp
/work/linuxdeploy-extracted/AppRun \
  --appdir "$APPDIR" \
  -e /app/bin/focus \
  -l /app/lib64/libOkular6Core.so.5.0.0 \
  -l /app/lib64/plugins/kf6/parts/focuspart.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_poppler.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_epub.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_mobi.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_md.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_txt.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_comicbook.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_xps.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_tiff.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_djvu.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_fax.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_fb.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_dvi.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_ghostview.so \
  -l /app/lib64/plugins/focus_generators/okularGenerator_kimgio.so \
  -d "$APPDIR/org.kde.focus.desktop" \
  -i "$APPDIR/focus.png" \
  --plugin qt \
  --output appimage || echo "LINUXDEPLOY_DONE(or appimage-plugin failed as expected)"
# --- extra Qt platform/wayland plugins linuxdeploy skips ---
cp -av /usr/lib64/qt6/plugins/platforms/* "$APPDIR/usr/plugins/platforms/"
for d in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client generic egldeviceintegrations; do
  [ -d "/usr/lib64/qt6/plugins/$d" ] && cp -av "/usr/lib64/qt6/plugins/$d" "$APPDIR/usr/plugins/"
done
# --- libs linuxdeploy excludes but Fedora-newer-than-host needs ---
cp -v /usr/lib64/libwayland-client.so* /usr/lib64/libwayland-server.so* "$APPDIR/usr/lib/"
cp -v /usr/lib64/libxcb-cursor.so* /usr/lib64/libxcb-util.so* /usr/lib64/libxcb.so* "$APPDIR/usr/lib/"
# --- restore pristine libs WITHOUT patchelf (rpath rewrites zero first pages;
# AppRun's LD_LIBRARY_PATH resolves everything, so no rpath is needed) ---
is_elf() { [ -f "$1" ] && [ ! -L "$1" ] && head -c 4 "$1" 2>/dev/null | grep -q $'^\x7fELF'; }
find "$APPDIR" -type f ! -path "*/AppDir/usr/share/*" ! -name "*.desktop" ! -name "*.png" ! -name "*.svg" ! -name "*.qml*" ! -name "*.js" ! -name "*.css" ! -name "*.xml" ! -name "*.txt" ! -name "*.conf" ! -name "*.json" ! -name "*.rc" ! -name "*.categories" ! -name "*.kcfg*" ! -name "*.ui" ! -name "*.ts" ! -name "*.qm" ! -name "*.dat" ! -name "*.ttf" ! -name "*.otf" | while read -r f; do
    is_elf "$f" || continue
    rel="${f#$APPDIR/}"
    pristine=""
    case "$rel" in
        usr/lib64/plugins/*)
            sub="${rel#usr/lib64/plugins/}"
            [ -e "/app/lib64/plugins/$sub" ] && pristine="/app/lib64/plugins/$sub" || continue ;;
        usr/lib64/*)
            sub="${rel#usr/lib64/}"
            if [ -e "/usr/lib64/$sub" ]; then pristine="/usr/lib64/$sub"
            elif [ -e "/app/lib64/$sub" ]; then pristine="/app/lib64/$sub"
            else continue; fi ;;
        usr/lib/*)
            sub="${rel#usr/lib/}"
            if [ -e "/usr/lib64/$sub" ]; then pristine="/usr/lib64/$sub"
            elif [ -e "/usr/lib64/pulseaudio/$sub" ]; then pristine="/usr/lib64/pulseaudio/$sub"
            elif [ -e "/usr/lib64/samba/$sub" ]; then pristine="/usr/lib64/samba/$sub"
            else
                base="$(basename "$sub")"
                hit="$(find /app/lib64 -name "$base" 2>/dev/null | head -n 1)"
                [ -n "$hit" ] && pristine="$hit" || continue
            fi ;;
        usr/bin/*)
            base="$(basename "$rel")"
            if [ -e "/usr/bin/$base" ]; then pristine="/usr/bin/$base"
            elif [ -e "/app/bin/$base" ]; then pristine="/app/bin/$base"
            else continue; fi ;;
        usr/plugins/*)
            sub="${rel#usr/plugins/}"
            [ -e "/usr/lib64/qt6/plugins/$sub" ] && pristine="/usr/lib64/qt6/plugins/$sub" || continue ;;
        *) continue ;;
    esac
    rm -f "$f"
    cp -f "$pristine" "$f" && chmod 755 "$f"
done
# --- drop crashing ibus input-context plugin ---
rm -rf "$APPDIR/usr/plugins/platforminputcontexts"
# --- launcher with Docker-parity env ---
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<'EOS'
#!/bin/sh
APPDIR="$(dirname "$(readlink -f "$0")")"
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$APPDIR/usr/lib64/plugins:$APPDIR/usr/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
export XDG_DATA_DIRS="$APPDIR/usr/share:/usr/local/share:/usr/share"
export XDG_CONFIG_DIRS="$APPDIR/usr/etc/xdg:/etc/xdg"
if [ -z "$QT_QPA_PLATFORMTHEME" ]; then
  export QT_QPA_PLATFORMTHEME=generic
fi
exec "$APPDIR/usr/bin/focus" "$@"
EOS
chmod +x "$APPDIR/AppRun"
# --- smoke test: must print help (fails build on segfault) ---
set -o pipefail
QT_QPA_PLATFORM=offscreen "$APPDIR/AppRun" --help 2>&1 | head -n 5
# --- pack (appimagetool needs `file`) ---
dnf -y install file >/tmp/dnf.log 2>&1
/work/appimagetool-extracted/AppRun /work/AppDir /work/Focus-Custom-x86_64.AppImage 2>&1 | tail -n 3
ls -lh /work/*.AppImage
chown 1000:1000 /work/Focus-Custom-x86_64.AppImage
ls -lh /work/*.AppImage
