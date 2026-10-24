#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Check that a macOS application bundle is complete and self-contained.
#
#   libs/platforms/macos/packaging/verify_bundle.sh <WordJewelsSolver.app | package.dmg>
#       [--version <x.y.z>] [--smoke] [--signed]
#
# A disk image is mounted read-only and its .app checked. What it looks for:
#
#   * Info.plist is valid, names the identifier, version, icon and the camera usage
#     text (a program that asks for the camera without it is ended by macOS);
#   * the icon, the Qt frameworks and plugins (the cocoa platform plugin) and the
#     QML modules (QtQuick, QtQuick3D) are in the bundle - Qt's deploy script put
#     them there, and this is the proof;
#   * nothing in the bundle links to a path outside the system and the bundle: no
#     /Users/runner, no Qt kit directory (otool -L over every Mach-O file);
#   * the dictionary and the dice are beside the executable;
#   * with --signed: codesign --verify --deep --strict, and the hardened runtime;
#   * with --smoke: the bundle's executable starts offscreen and presents frames.
#
# macOS only (plutil, otool, codesign, hdiutil). Unverified until the release
# workflow's macOS leg runs it.
set -euo pipefail

target=""
version=""
smoke=false
signed=false
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="${2:?}"; shift 2 ;;
    --smoke) smoke=true; shift ;;
    --signed) signed=true; shift ;;
    -h|--help) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "verify_bundle.sh: unknown argument '$1'" >&2; exit 2 ;;
    *) target="$1"; shift ;;
  esac
done
[ -e "$target" ] || { echo "usage: verify_bundle.sh <app|dmg> [--version v] [--smoke] [--signed]" >&2; exit 2; }
fail() { echo "FAILED: $*" >&2; exit 1; }

mount_point=""
cleanup() {
  if [ -n "$mount_point" ]; then hdiutil detach "$mount_point" -quiet >/dev/null 2>&1 || true; fi
}
trap cleanup EXIT

app="$target"
case "$target" in
  *.dmg)
    mount_point="$(mktemp -d)"
    hdiutil attach -nobrowse -readonly -mountpoint "$mount_point" "$target" -quiet
    app="$(find "$mount_point" -maxdepth 2 -name '*.app' -type d | head -n 1)"
    [ -n "$app" ] || fail "no .app in $target"
    ;;
esac
echo "==> $app"

plist="$app/Contents/Info.plist"
[ -f "$plist" ] || fail "no Contents/Info.plist"
plutil -lint "$plist" >/dev/null || fail "Info.plist is not a valid property list"
key() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }

[ "$(key CFBundleIdentifier)" = "org.iainchesworth.wordjewelssolver" ] || fail "CFBundleIdentifier is '$(key CFBundleIdentifier)'"
[ "$(key CFBundleExecutable)" = "WordJewelsSolver" ] || fail "CFBundleExecutable is '$(key CFBundleExecutable)'"
[ -n "$(key NSCameraUsageDescription)" ] || fail "Info.plist has no NSCameraUsageDescription"
[ "$(key CFBundleIconFile)" = "AppIcon.icns" ] || fail "CFBundleIconFile is '$(key CFBundleIconFile)'"
if [ -n "$version" ]; then
  [ "$(key CFBundleShortVersionString)" = "$version" ] || fail "CFBundleShortVersionString is '$(key CFBundleShortVersionString)', expected $version"
fi
echo "    Info.plist: identifier, executable, icon, camera text"

macos="$app/Contents/MacOS"
for required in \
    "$app/Contents/Resources/AppIcon.icns" \
    "$macos/WordJewelsSolver" \
    "$macos/dictionaries/en.txt" \
    "$macos/dice/3x3.txt" \
    "$macos/NOTICES.txt"; do
  [ -e "$required" ] || fail "missing ${required#"$app"/}"
done

# Qt, deployed into the bundle by qt_generate_deploy_qml_app_script.
for required in \
    "Contents/Frameworks/QtCore.framework" \
    "Contents/Frameworks/QtQuick.framework" \
    "Contents/Frameworks/QtQuick3D.framework" \
    "Contents/PlugIns/platforms/libqcocoa.dylib" \
    "Contents/Resources/qml/QtQuick" \
    "Contents/Resources/qml/QtQuick3D"; do
  [ -e "$app/$required" ] || fail "the bundle has no $required: Qt was not deployed into it"
done
echo "    Qt: frameworks, the cocoa platform plugin and the QML modules are in the bundle"

# No link to anything that exists only on the build machine.
bad=0
while IFS= read -r file; do
  if file "$file" | grep -q 'Mach-O'; then
    leaks="$(otool -L "$file" | tail -n +2 | awk '{print $1}' | grep -Ev '^(@rpath/|@executable_path/|@loader_path/|/System/|/usr/lib/)' || true)"
    if [ -n "$leaks" ]; then
      echo "    $file links outside the system and the bundle:" >&2
      echo "$leaks" | while IFS= read -r leak; do echo "        $leak" >&2; done
      bad=1
    fi
  fi
done < <(find "$app" -type f)
[ "$bad" = 0 ] || fail "the bundle depends on files that are not in it"
echo "    otool -L: every Mach-O file links only the system and the bundle"

if [ "$signed" = true ]; then
  codesign --verify --deep --strict --verbose=2 "$app" || fail "codesign --verify failed"
  codesign -d --verbose=2 "$app" 2>&1 | grep -q 'flags=.*runtime' || fail "the bundle is not signed with the hardened runtime"
  echo "    codesign: verified, hardened runtime"
fi

if [ "$smoke" = true ]; then
  out="$(mktemp)"
  QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=opengl "$macos/WordJewelsSolver" --smoke 10 --store "$(mktemp -u)" | tee "$out"
  grep -Eq 'smoke: frames=[0-9]+' "$out" || fail "the bundle's program printed no frame report"
fi
echo "OK: $(basename "$app")"
