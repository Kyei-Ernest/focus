# Focus (customized) – Universal Document Viewer

> Custom fork of KDE Okular, rebranded as Focus. Upstream: https://invent.kde.org/graphics/okular.git
>
> **What changed here:**
> * Pinned **Library tab** (`shell/shell.cpp/h`): tab 0 is always the library
>   and the startup page; document tabs start at 1 (`m_tabs[N-1]`). The tab
>   can't be closed or dragged away, wraps into Next/Previous-tab cycling,
>   and closing the last document returns to it. No welcome screen.
> * Library UI (`shell/welcomescreen.ui/.cpp/.h`, `gui/recentitemsmodel.*`):
>   search, format chips (`All / PDFs / EPUBs / Markdown / Others`), sort
>   (`Recently opened / Name / Size`), `▶ Continue` strip for the newest
>   file, `Library · N documents` counts, list/grid toggle, rich rows
>   (format badge, `FORMAT · size`, `Read ›`), single-click opens.
> * Warm-dark theme (`#1E1A15` + amber `#E8A33D`): library page (guaranteed
>   by `paintEvent` fill), dark tab pane, major amber Library tab with book
>   icon, full-width library (branding panel hidden, `+ Open` header button).
>   Theme entry points: `applyJotaLabTheme()` in `shell/welcomescreen.cpp`,
>   tab strip stylesheet in `Shell` constructor (`shell/shell.cpp`).
> See `shell/welcomescreen.ui`, `shell/welcomescreen.cpp`,
> `shell/welcomescreen.h`, `gui/recentitemsmodel.h`, `gui/recentitemsmodel.cpp`.
>
> Theme entry point: `applyJotaLabTheme()` in `shell/welcomescreen.cpp`
> (palette + stylesheet; the document viewer is untouched). The list view and
> its viewport get the palette set explicitly — `QListView` does not inherit
> a custom parent palette. Section header renamed to `Library`.

Focus can view and annotate documents of various formats, including PDF,
Postscript, Comic Book, and various image formats. It supports native PDF
annotations.

## How to run (this fork)

### 0. Quick path on this laptop (Docker, single commands)

Native build needs Qt ≥ 6.6 / ECM 5.240 / Poppler-Qt6 ≥ 24.08, which older
distros (e.g. Ubuntu 24.04) don't ship — so this fork runs in a Fedora
container. Everything lives in `~/okular-demo/` (verified to exist):

| Path | What it is |
|---|---|
| `~/okular-demo/Makefile` | single-command runner (see below) |
| `~/okular-demo/run-okular.sh` | launcher used by `make run` |
| `~/okular-demo/data/` | your documents; open as `/data/<name>` |
| `~/okular-demo/home/` | app home (recents in `.config/okularrc`) |
| `~/Downloads/okular-master/` | your customized source tree |
| docker image `okular-custom:lib1` | Fedora 42 + toolchain + installed build |

```bash
cd ~/okular-demo
make run                   # launch the app (library screen)
make open FILE=/data/x.pdf # launch with a document open
make stop                  # stop it
make status                # is it running?
make logs                  # recent app output
make rebuild               # recompile source edits into a fresh image, then relaunch
make reseed                # restore the 4 sample documents
```

Put your own files in `~/okular-demo/data/` and open them via `File > Open`
or `make open FILE=/data/mybook.pdf`. Your full host home is also mounted
inside the app at `/host-home` (shortcut `Host-Files` in the file dialog's
home folder) — without it the container's file picker can't see your files.
Notes:

* First `make rebuild` takes ~15 min (full compile); later ones recompile
  only changed files (seconds).
* `make rebuild` installs into `~/okular-demo/app/`, which is mounted at
  `/app` in both the build and run containers — no image commits, so
  rebuilds add ~0 bytes of docker storage (previously each `docker commit`
  left a ~800 MB dangling layer).
* If the window doesn't appear: `make logs`, and check the Wayland socket
  still exists at `/run/user/1000/wayland-0`.
* The stock `snap run okular` is a different, unmodified app — if it shows
  `command-chain ... not found`, reconnect it with
  `sudo snap connect okular:kf6-core24 kf6-core24:kf6-core24` and
  `sudo snap connect okular:gpu-2404 mesa-2404:gpu-2404`.

### 1. Prerequisites (native build)

* CMake >= 3.22, C++20 compiler (GCC 13 / Clang 16+)
* Qt6 >= 6.6: `Core Widgets PrintSupport Svg Xml DBus` (+ `Qml Quick` for mobile)
* KDE Frameworks >= 6.0 / 5.240: `Archive Bookmarks Completion Config
  ConfigWidgets CoreAddons Crash I18n KIO ThreadWeaver WindowSystem XmlGui
  IconThemes Parts ColorScheme TextWidgets WidgetsAddons`
* `extra-cmake-modules` (ECM), `zlib`
* Recommended backends: `Poppler-Qt6 >= 24.08` (PDF), `Discount` (Markdown),
  `EPub / QMobipocket6` (ebooks), `DjVuLibre`, `TIFF`, `LibSpectre` (PS),
  `KExiv2Qt6` (images)

Full dependency list is in `CMakeLists.txt:11-12` and `.kde-ci.yml`.
General KDE build setup: https://community.kde.org/Get_Involved/development

Ubuntu 24.04 example (adjust to your release):

```bash
sudo apt install cmake extra-cmake-modules gettext \
  qt6-base-dev qt6-svg-dev qt6-multimedia-dev \
  libkf6archive-dev libkf6bookmarks-dev libkf6completion-dev \
  libkf6config-dev libkf6configwidgets-dev libkf6coreaddons-dev \
  libkf6crash-dev libkf6i18n-dev libkf6kio-dev libkf6threadweaver-dev \
  libkf6windowsystem-dev libkf6xmlgui-dev libkf6iconthemes-dev \
  libkf6parts-dev libkf6colorscheme-dev libkf6textwidgets-dev \
  libkf6widgetsaddons-dev libpoppler-qt6-dev zlib1g-dev
```

### 2. Configure + build

```bash
git clone <your-fork-url> okular-custom
cd okular-custom
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX=$HOME/.local \
  -DOKULAR_UI=desktop
cmake --build build -j$(nproc)
```

`OKULAR_UI` values: `desktop`, `mobile`, `both` (`CMakeLists.txt:14`).

### 3. Install + run

```bash
cmake --install build
source build/prefix.sh   # puts $HOME/.local first so the new build is picked up
~/.local/bin/okular                  # welcome / library screen
~/.local/bin/okular document.pdf     # open a file directly
~/.local/bin/okular file1.pdf file2.epub
```

Without installing, you can try the freshly built shell (some generators may
be missing until installed):

```bash
source build/prefix.sh
./build/shell/okular --help
./build/shell/okular
```

To exercise the new library UI: launch with no arguments, open a few
PDF/EPUB/MD files (`File > Open`, or pass them on the command line), then
return to the welcome screen by closing all tabs. Use the search box and the
`All / PDFs / EPUBs / Markdown / Others` chips to filter.

### 4. Tests

```bash
cmake --build build --target test   # or: ctest --test-dir build
```

Upstream notes: tests expect an installed tree (`make install` /
`cmake --install`) plus `source build/prefix.sh` before running manually.

### 5. Troubleshooting

* `Could not find ECM`: install `extra-cmake-modules` and delete `build/`.
* Wrong Okular launched: you forgot `source build/prefix.sh`, or system
  `/usr/bin/okular` shadows `~/.local/bin/okular` — check `which -a okular`.
* Missing PDF support: `Poppler-Qt6` not found at configure time — install
  `libpoppler-qt6-dev` and re-configure.
* Missing icons/styles on custom prefix: re-run `source build/prefix.sh`.

---

## Upstream docs (kept)

### Downloads

For download and installation instructions, see https://okular.kde.org/download.php

### User manual

https://docs.kde.org/?application=okular&branch=stable5

### Bugs

https://bugs.kde.org/buglist.cgi?product=okular

Please report bugs on Bugzilla (https://bugs.kde.org/enter_bug.cgi?product=okular), and not on our GitLab instance (https://invent.kde.org).

### Mailing list

https://mail.kde.org/mailman/listinfo/okular-devel

### Source code

https://invent.kde.org/graphics/okular.git

The Okular repository contains the source code for:
 * the `okular` desktop application (the “shell”),
 * the `okularpart` KParts plugin,
 * the `okularkirigami` mobile application,
 * several `okularGenerator_xyz` plugins, which provide backends for different document types.

### Apidox

https://api.kde.org/okular/html/index.html

## Contributing

Okular uses the merge request workflow.
Merge requests are required to run pre-commit CI jobs; please don’t push to the master branch directly.
See https://community.kde.org/Infrastructure/GitLab for an introduction.

### Build instructions (upstream)

Okular can be built like many other applications developed by KDE.
See https://community.kde.org/Get_Involved/development for an introduction.

If your build environment is set up correctly, you can also build Okular using CMake:

```bash
git clone https://invent.kde.org/graphics/okular.git
cd okular
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir ..
make
make install
```

Okular also builds tests in the build tree. To run them, you have to run `make install` first.

If you install Okular in a different path than your system install directory it is possible that you need to run

```bash
source prefix.sh
```

so that the correct Okular instance and libraries are picked up.
Afterwards one can run `okular` inside the shell instance.
The source command is also required to run the tests manually.

As stated above, Okular has various build targets.
Two of them are executables.
You can choose which executable to build by passing a flag to CMake:

```bash
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir -DOKULAR_UI=desktop ..
```
Available options are `desktop`, `mobile`, and `both`.

### clang-format

The Okular project uses clang-format to enforce source code formatting.
See [README.clang_format](https://invent.kde.org/graphics/okular/-/blob/master/README.clang-format) for more information.
