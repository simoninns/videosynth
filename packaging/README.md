# Packaging

Desktop-integration and distribution files for videosynth. The application ID
used throughout is `io.github.simoninns.VideoSynth`; it names the desktop
entry, the AppStream metadata, the installed icons and the Flatpak, and is also
passed to `QApplication::setDesktopFileName()` in [../src/gui/main.cpp](../src/gui/main.cpp)
so desktop shells associate a running window with its menu entry.

## Contents

| Path | Purpose |
| --- | --- |
| `linux/io.github.simoninns.VideoSynth.desktop` | Freedesktop menu entry for the GUI |
| `linux/io.github.simoninns.VideoSynth.metainfo.xml.in` | AppStream metadata; CMake substitutes the release version and date |
| `flatpak/io.github.simoninns.VideoSynth.yml` | Flatpak manifest |

Icons are not duplicated here: the installed icon theme entries are renamed
copies of `assets/videosynth-icon-<size>.png` and `assets/videosynth-logo.svg`,
which [../scripts/render-icons.sh](../scripts/render-icons.sh) regenerates from
the SVG.

## Desktop integration

`cmake --install` places, when the GUI is built:

```
share/applications/io.github.simoninns.VideoSynth.desktop
share/metainfo/io.github.simoninns.VideoSynth.metainfo.xml
share/icons/hicolor/{16x16,32x32,48x48,64x64,128x128,256x256}/apps/io.github.simoninns.VideoSynth.png
share/icons/hicolor/scalable/apps/io.github.simoninns.VideoSynth.svg
share/videosynth/assets/…
```

That is what makes the application appear in the GNOME application grid (and
other XDG menus). Two CMake cache variables feed the AppStream release entry
and default to the build version and the current UTC date:

```bash
cmake -S . -B build \
    -DVIDEOSYNTH_RELEASE_VERSION=1.2.0 \
    -DVIDEOSYNTH_RELEASE_DATE=2026-08-05
```

## Flatpak

The manifest builds both front ends and makes the GUI the default entry point.
The CLI ships alongside it:

```bash
flatpak run io.github.simoninns.VideoSynth                     # GUI
flatpak run --command=videosynth io.github.simoninns.VideoSynth --help
```

### Permissions

`--filesystem=host`, plus `/media` and `/run/media` for removable volumes.
Generation reads source media and writes multi-gigabyte sample files anywhere
the user chooses, so the sandbox is given unrestricted read/write access to the
user's own filesystem rather than a portal-mediated subset.

### Modules

`yaml-cpp`, `spdlog`, `Imath` and `OpenEXR` are built into `/app` because the
KDE runtime does not carry them. `sqlite3` and `zlib` come from the runtime.
FFmpeg is built as well: progressive MKV sources are decoded by shelling out to
the `ffmpeg` binary and probed with `ffprobe`, and no Flatpak runtime provides
either program.

The videosynth module turns off the test suites, the clang-format gate and the
clang-tidy gate (`VIDEOSYNTH_BUILD_TESTS`, `VIDEOSYNTH_ENABLE_CLANG_FORMAT`,
`VIDEOSYNTH_ENABLE_CLANG_TIDY`): those are development and CI concerns, and the
SDK's tooling versions differ from the dev shell's. It also points
`VIDEOSYNTH_BUNDLED_ASSET_DIR` at `/app/share/videosynth/assets` so the
`{bundled}` asset root resolves inside the sandbox.

### Building locally

The build sandbox has no git metadata, so the version string and the AppStream
release date are substituted into a generated copy of the manifest first:

```bash
scripts/prepare-flatpak-manifest.py \
    --version v1.2.0 --date 2026-08-05 \
    --output flatpak-build/io.github.simoninns.VideoSynth.yml

flatpak remote-add --user --if-not-exists flathub \
    https://dl.flathub.org/repo/flathub.flatpakrepo

flatpak-builder --user --force-clean --install-deps-from=flathub \
    --repo=flatpak-repo \
    flatpak-build/build flatpak-build/io.github.simoninns.VideoSynth.yml

flatpak build-bundle flatpak-repo videosynth.flatpak io.github.simoninns.VideoSynth
flatpak install --user videosynth.flatpak
```

Building the committed manifest directly also works; the version then reads
back literally as the unsubstituted placeholder.

### Continuous integration

[../.github/workflows/flatpak.yml](../.github/workflows/flatpak.yml) builds the
bundle on every push and pull request and uploads it as a workflow artefact, so
packaging breakage surfaces before a release is cut. The bundle is only
attached to a GitHub release when the build was triggered by a `v*` tag.

Additional architectures are added by extending the `matrix.include` list in
that workflow with an arch and a matching runner label.
