#!/bin/bash
# Fix zeroed first pages in AppDir libs: restore pristine copies WITHOUT
# running patchelf (AppRun's LD_LIBRARY_PATH resolves everything, so no
# rpath is needed). Also drops the unused libcrypt link and installs the
# usbmuxd stub (kept from the previous fix, harmless if unused).
# Runs INSIDE okular-custom:lib1 with /work (AppDir) and /app mounted.
set -u
APPDIR=/work/AppDir
LOG=/work/fix-pristine.log
: > "$LOG"
fixed=0
skipped=0
is_elf() { [ -f "$1" ] && [ ! -L "$1" ] && head -c 4 "$1" 2>/dev/null | grep -q $'^\x7fELF'; }
find "$APPDIR" -type f ! -path "*/AppDir/usr/share/*" ! -name "*.desktop" ! -name "*.png" ! -name "*.svg" ! -name "*.qml*" ! -name "*.js" ! -name "*.css" ! -name "*.xml" ! -name "*.txt" ! -name "*.conf" ! -name "*.json" ! -name "*.rc" ! -name "*.categories" ! -name "*.kcfg*" ! -name "*.ui" ! -name "*.ts" ! -name "*.qm" ! -name "*.dat" ! -name "*.ttf" ! -name "*.otf" | while read -r f; do
    is_elf "$f" || continue
    rel="${f#$APPDIR/}"
    pristine=""
    case "$rel" in
        usr/lib64/plugins/*)
            sub="${rel#usr/lib64/plugins/}"
            [ -e "/app/lib64/plugins/$sub" ] && pristine="/app/lib64/plugins/$sub" || { echo "SKIP: $rel" >> "$LOG"; continue; } ;;
        usr/lib64/*)
            sub="${rel#usr/lib64/}"
            if [ -e "/usr/lib64/$sub" ]; then pristine="/usr/lib64/$sub"
            elif [ -e "/app/lib64/$sub" ]; then pristine="/app/lib64/$sub"
            else echo "SKIP: $rel" >> "$LOG"; continue; fi ;;
        usr/lib/*)
            sub="${rel#usr/lib/}"
            if [ -e "/usr/lib64/$sub" ]; then pristine="/usr/lib64/$sub"
            else
                base="$(basename "$sub")"
                hit="$(find /app/lib64 -name "$base" 2>/dev/null | head -n 1)"
                [ -n "$hit" ] && pristine="$hit" || { echo "SKIP: $rel" >> "$LOG"; continue; }
            fi ;;
        usr/bin/*)
            base="$(basename "$rel")"
            if [ -e "/usr/bin/$base" ]; then pristine="/usr/bin/$base"
            elif [ -e "/app/bin/$base" ]; then pristine="/app/bin/$base"
            else echo "SKIP: $rel" >> "$LOG"; continue; fi ;;
        usr/plugins/*)
            sub="${rel#usr/plugins/}"
            [ -e "/usr/lib64/qt6/plugins/$sub" ] && pristine="/usr/lib64/qt6/plugins/$sub" || { echo "SKIP: $rel" >> "$LOG"; continue; } ;;
        *) echo "SKIP: $rel" >> "$LOG"; continue ;;
    esac
    rm -f "$f"
    cp -f "$pristine" "$f" && chmod 755 "$f" && echo "RESTORED: $rel <= $pristine" >> "$LOG" || echo "FAIL: $rel" >> "$LOG"
done
echo "---- summary ----"
grep -c "^RESTORED" "$LOG" | xargs echo "restored:"
grep -c "^SKIP" "$LOG" | xargs echo "skipped:"
grep -c "^FAIL" "$LOG" | xargs echo "failed:"
# drop unused libcrypt link + lib itself (segfaults on Ubuntu, unused by sasl2)
if [ -f "$APPDIR/usr/lib/libsasl2.so.3" ]; then
    patchelf --remove-needed libcrypt.so.2 "$APPDIR/usr/lib/libsasl2.so.3" 2>/dev/null || echo "note: patchelf remove-needed failed"
fi
rm -f "$APPDIR/usr/lib/libcrypt.so.2"
# verify: no zeroed DT_INIT targets remain
echo "---- zeroed INIT check (want empty) ----"
bad=0
for f in $(find "$APPDIR/usr/lib" "$APPDIR/usr/bin" -type f 2>/dev/null); do
  is_elf "$f" || continue
  init=$(readelf -d "$f" 2>/dev/null | grep -E "\(INIT\)" | awk '{print $NF}')
  if [ -n "$init" ] && [ "$init" != "0x0" ]; then
    off=$((init))
    if [ "$off" -lt 1000000 ]; then
      bytes=$(dd if="$f" bs=1 skip=$off count=8 2>/dev/null | od -A n -t x1 | tr -d ' \n')
      if [ "$bytes" = "0000000000000000" ]; then echo "ZEROED: ${f#$APPDIR/} INIT=$init"; bad=$((bad+1)); fi
    fi
  fi
done
echo "zeroed files: $bad"
