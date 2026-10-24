#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build the Linux application as an AppImage: one file that carries Qt (the Qt Quick
# runtime, Quick 3D, the QML modules and the plugins the application loads) and runs
# on a distribution without any of it.
#
#   libs/platforms/linux/packaging/build_appimage.sh
#       --build <configured and built Release tree> --version <x.y.z[-pre]>
#       --out <output directory> [--qt-prefix <Qt kit dir, e.g. ~/Qt/6.12.0/gcc_64>]
#       [--tools <dir for linuxdeploy>] [--smoke]
#
# What it does:
#   1. gets linuxdeploy and its Qt plugin (linuxdeploy.pins: pinned tag and hash)
#      into --tools;
#   2. `cmake --install` of the tree into an AppDir, then the desktop entry, the
#      AppStream metadata (with this version) and the icons beside it, and
#      validates the entry and the metadata when desktop-file-validate and
#      appstreamcli are installed;
#   3. runs linuxdeploy with its Qt plugin over the AppDir, which finds the Qt
#      libraries, plugins and QML modules the program needs by reading the program
#      and the application's QML, and writes
#      <out>/WordJewelsSolver-<version>-x86_64.AppImage;
#   4. with --smoke, runs that AppImage's `--smoke 10` offscreen.
#
# The AppImage holds the window; wjs-cli is in the tar.gz/ZIP/deb/rpm of the
# command-line package. Needs a Linux host with the Qt kit and the libraries Qt
# itself needs (docs/ci.md, "Releases"). Unverified until the release rehearsal
# runs it: nothing here has been built on a machine with a Qt kit for Linux.
set -euo pipefail

build=""
version=""
out=""
qt_prefix="${QT_ROOT_DIR:-}"
tools=""
smoke=false
while [ $# -gt 0 ]; do
  case "$1" in
    --build) build="${2:?}"; shift 2 ;;
    --version) version="${2:?}"; shift 2 ;;
    --out) out="${2:?}"; shift 2 ;;
    --qt-prefix) qt_prefix="${2:?}"; shift 2 ;;
    --tools) tools="${2:?}"; shift 2 ;;
    --smoke) smoke=true; shift ;;
    -h|--help) sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "build_appimage.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done
if [ ! -d "$build" ] || [ -z "$version" ] || [ -z "$out" ]; then
  echo "build_appimage.sh: --build, --version and --out are required" >&2
  exit 2
fi
if [ ! -x "$qt_prefix/bin/qmake" ]; then
  echo "build_appimage.sh: no Qt kit at '$qt_prefix' (pass --qt-prefix, or set QT_ROOT_DIR): the Qt plugin of linuxdeploy needs its qmake" >&2
  exit 2
fi

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../../.." && pwd)"
id="org.iainchesworth.wordjewelssolver"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
[ -n "$tools" ] || tools="$work/tools"
mkdir -p "$tools" "$out"
out="$(cd "$out" && pwd)"

# --- 1. the tools --------------------------------------------------------------
pending=false
while read -r name url pin; do
  case "$name" in ''|'#'*) continue ;; esac
  if [ ! -x "$tools/$name" ]; then
    echo "==> downloading $name"
    curl --fail --silent --show-error --location --output "$tools/$name" "$url"
    chmod +x "$tools/$name"
  fi
  actual="$(sha256sum "$tools/$name" | cut -d' ' -f1)"
  if [ "$pin" = PENDING ]; then
    pending=true
    echo "::warning::$name is not pinned yet (trust on first use). SHA-256: $actual. Run libs/platforms/linux/packaging/pin_linuxdeploy.sh and commit linuxdeploy.pins." >&2
  elif [ "$actual" != "$pin" ]; then
    echo "build_appimage.sh: $name has SHA-256 $actual, but linuxdeploy.pins says $pin. Refusing to run it." >&2
    exit 1
  fi
done < "$here/linuxdeploy.pins"
if [ "$pending" = false ]; then echo "==> the linuxdeploy tools match their pinned hashes"; fi

# --- 2. the AppDir -------------------------------------------------------------
appdir="$work/AppDir"
cmake --install "$build" --prefix "$appdir/usr"
install -D -m 0644 "$here/$id.desktop" "$appdir/usr/share/applications/$id.desktop"
date_iso="$(date -u +%F)"
install -d "$appdir/usr/share/metainfo"
sed -e "s/@VERSION@/$version/g" -e "s/@DATE@/$date_iso/g" "$here/$id.metainfo.xml.in" > "$appdir/usr/share/metainfo/$id.metainfo.xml"
install -D -m 0644 "$repo/assets/icon/app.png" "$appdir/usr/share/icons/hicolor/512x512/apps/$id.png"
install -D -m 0644 "$repo/assets/icon/app-256.png" "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png"

if command -v desktop-file-validate >/dev/null 2>&1; then
  desktop-file-validate "$appdir/usr/share/applications/$id.desktop"
else
  echo "note: desktop-file-validate is not installed; the desktop entry was not validated" >&2
fi
if command -v appstreamcli >/dev/null 2>&1; then
  appstreamcli validate --no-net "$appdir/usr/share/metainfo/$id.metainfo.xml"
else
  echo "note: appstreamcli is not installed; the AppStream metadata was not validated" >&2
fi

# --- 3. linuxdeploy ------------------------------------------------------------
# QML_SOURCES_PATHS is every directory with QML in it: the plugin reads the
# imports there to know which QML modules to copy (Quick 3D, Controls, Layouts,
# Multimedia for the camera's live view).
qml_paths="$(find "$repo/apps/app/src" -name '*.qml' -printf '%h\n' | sort -u | paste -sd: -)"
export QMAKE="$qt_prefix/bin/qmake"
export QML_SOURCES_PATHS="$qml_paths"
export LD_LIBRARY_PATH="$qt_prefix/lib:${LD_LIBRARY_PATH:-}"
export LINUXDEPLOY_OUTPUT_VERSION="$version"
export APPIMAGE_EXTRACT_AND_RUN=1   # no FUSE on a hosted runner
export NO_STRIP=1                   # the strip bundled with linuxdeploy fails on newer ELF sections
export PATH="$tools:$PATH"

(
  cd "$work"
  "$tools/linuxdeploy-x86_64.AppImage" --appdir "$appdir" \
    --executable "$appdir/usr/bin/WordJewelsSolver" \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/512x512/apps/$id.png" \
    --plugin qt --output appimage
)

built="$(find "$work" -maxdepth 1 -name '*.AppImage' | head -n 1)"
[ -n "$built" ] || { echo "build_appimage.sh: linuxdeploy wrote no AppImage" >&2; exit 1; }
final="$out/WordJewelsSolver-$version-x86_64.AppImage"
mv "$built" "$final"
chmod +x "$final"
echo "==> $final ($(du -h "$final" | cut -f1))"

# --- 4. smoke ------------------------------------------------------------------
if [ "$smoke" = true ]; then
  echo "==> the AppImage starts and presents frames, offscreen"
  QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=opengl "$final" --smoke 10 --store "$work/smoke-store.txt" | tee "$work/smoke.txt"
  grep -Eq 'smoke: frames=[0-9]+' "$work/smoke.txt" || { echo "build_appimage.sh: the AppImage printed no frame report" >&2; exit 1; }
fi
