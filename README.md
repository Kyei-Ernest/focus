# focus-demo — Focus rebrand workspace (separated from okular-demo)

Your Focus work was sharing `~/Downloads/okular-master`, `~/okular-demo/build`,
`~/okular-demo/app` and `~/okular-demo/appimage-build` with the Okular packaging.
It now lives here so the two projects can't clobber each other.

## Layout

| Path | What |
|---|---|
| `src/` | Focus source tree (copy of okular-master at separation time) |
| `build/` | Your CMake build tree (moved from `~/okular-demo/build`) |
| `app/` | Your install prefix (moved from `~/okular-demo/app`, mounted as `/app`) |
| `packaging/` | `build-focus-inside.sh`, `fix-pristine-inside.sh`, linuxdeploy/appimagetool + extracted dirs (copied, self-sufficient) |
| `Focus-Custom-x86_64.AppImage` | Last built Focus AppImage |
| `run-focus.sh` | Docker launcher (`/app/bin/focus`, mounts this workspace) |

Shared (left in place, used by both): `~/okular-demo/data/`, `~/okular-demo/home/`
(host files + reading profile), `~/okular-demo/dictionary.sqlite`.

The offline dictionary (`Define` feature) looks up `okular/dictionary.sqlite`
under the XDG data dirs. For a fresh machine, copy that 27 MB SQLite file
(table `entries(word, definition)`) to `~/focus-demo/app/share/okular/`
before packing (or anywhere on `XDG_DATA_DIRS` under `okular/`).

## Commands (Focus)

```bash
# rebuild install tree from src (mirrors okular-demo Makefile, new paths)
docker rm -f focus-build 2>/dev/null
docker run -d --name focus-build -v "$HOME/focus-demo/src:/src:ro" \
  -v "$HOME/focus-demo/build:/build" -v "$HOME/focus-demo/app:/app" \
  -w /build okular-custom:lib1 sleep infinity
docker exec focus-build cmake -S /src -B /build -GNinja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/app \
  -DOKULAR_UI=desktop -DBUILD_TESTING=OFF \
  "-DFORCE_NOT_REQUIRED_DEPENDENCIES=KF6Wallet;KF6DocTools;Qt6Qml;KExiv2Qt6;KF6Purpose;Qt6TextToSpeech;Qt6::MultimediaWidgets"
docker exec focus-build cmake --build /build -j$(nproc)
docker exec focus-build cmake --install /build

# pack AppImage (adapted script, mounts this workspace as /work)
docker run --rm -v "$HOME/focus-demo/app:/app:ro" \
  -v "$HOME/focus-demo/packaging:/work" -w /tmp \
  -e ARCH=x86_64 okular-custom:lib1 bash /work/build-focus-inside.sh
cp ~/focus-demo/packaging/Focus-Custom-x86_64.AppImage ~/focus-demo/

# run
~/focus-demo/run-focus.sh
~/focus-demo/run-focus.sh /data/mybook.pdf   # needs a FILE= variant; or pass host path
```

Notes:
- `~/okular-demo/appimage-build/AppDir/` is now Okular's again — do not point
  Focus builds at it; `packaging/` has its own extracted tool dirs.
- `~/okular-demo/run-okular.sh` was restored to `/app/bin/okular`
  (Okular). Use `run-focus.sh` here for Focus.
