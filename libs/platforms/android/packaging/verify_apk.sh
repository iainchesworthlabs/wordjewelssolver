#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Looks inside a finished Android package and fails on anything the build was
# meant to rule out. It reads the package alone (aapt2 and apksigner from the SDK's
# build-tools), so it holds for what a person would install, not for what the
# build tree says: androiddeployqt merges Qt's own manifest rules, and the
# permissions Qt's core and network modules ask for by default are the thing to
# prove absent.
#
#   verify_apk.sh <package.apk> [--version <x.y.z>] [--release] [--signed]
#
#   --version  the versionName must be exactly this
#   --release  the package must not be debuggable and must not be signed with
#              Android's debug key
#   --signed   the package must carry a valid signature (the debug key's is one,
#              unless --release is also given)
#
# Run by android-build.yml (the debug-signed package) and release.yml (the signed
# one); tests/platform/check_android.cmake holds the manifest source to the same
# permission list. Exit 0 when everything holds, 1 when something does not, 2 for a
# bad call or a missing tool.
set -euo pipefail

apk=""
version=""
release=false
signed=false
while [ $# -gt 0 ]; do
  case "$1" in
    --version) version="${2:?--version needs a value}"; shift 2 ;;
    --release) release=true; shift ;;
    --signed) signed=true; shift ;;
    -h|--help) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "verify_apk.sh: unknown argument '$1'" >&2; exit 2 ;;
    *) [ -z "$apk" ] || { echo "verify_apk.sh: one package at a time" >&2; exit 2; }; apk="$1"; shift ;;
  esac
done
[ -n "$apk" ] || { echo "usage: verify_apk.sh <package.apk> [--version x.y.z] [--release] [--signed]" >&2; exit 2; }
[ -f "$apk" ] || { echo "verify_apk.sh: $apk is not a file" >&2; exit 2; }

application_id="org.iainchesworth.wordjewelssolver"
library="lib/arm64-v8a/libWordJewelsSolver_arm64-v8a.so"

# aapt2 and apksigner: on PATH, or the newest build-tools of the SDK.
find_tool() {
  local name="$1" found="" sdk dir
  if found="$(command -v "$name" 2>/dev/null)"; then printf '%s' "$found"; return 0; fi
  sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
  if [ -n "$sdk" ] && [ -d "$sdk/build-tools" ]; then
    # Newest first: build-tools directories are version numbers.
    while IFS= read -r dir; do
      for candidate in "$dir/$name" "$dir/$name.exe" "$dir/$name.bat"; do
        if [ -f "$candidate" ]; then printf '%s' "$candidate"; return 0; fi
      done
    done < <(find "$sdk/build-tools" -mindepth 1 -maxdepth 1 -type d | sort -Vr)
  fi
  return 1
}
aapt2="$(find_tool aapt2)" || { echo "verify_apk.sh: aapt2 not found (put the SDK's build-tools on PATH or set ANDROID_SDK_ROOT)" >&2; exit 2; }
apksigner="$(find_tool apksigner)" || { echo "verify_apk.sh: apksigner not found (put the SDK's build-tools on PATH or set ANDROID_SDK_ROOT)" >&2; exit 2; }

status=0
bad() { echo "FAIL: $*" >&2; status=1; }
ok() { echo "ok:   $*"; }

badging="$("$aapt2" dump badging "$apk" | tr -d '\r')"
permissions="$("$aapt2" dump permissions "$apk" | tr -d '\r')"

# The application id and the version.
if printf '%s\n' "$badging" | grep -q "^package: name='$application_id' "; then
  ok "the application id is $application_id"
else
  bad "the application id is not $application_id: $(printf '%s\n' "$badging" | sed -n 1p)"
fi
if [ -n "$version" ]; then
  if printf '%s\n' "$badging" | grep -q "^package: .* versionName='$version'"; then
    ok "versionName is $version"
  else
    bad "versionName is not $version: $(printf '%s\n' "$badging" | sed -n 1p)"
  fi
fi
code="$(printf '%s\n' "$badging" | sed -n "1s/.* versionCode='\([0-9]*\)'.*/\1/p")"
if [ -n "$code" ] && [ "$code" -gt 0 ]; then ok "versionCode is $code"; else bad "versionCode is missing or zero"; fi

# The platform levels: nothing older than Qt 6.12's floor, and a target that Google Play takes.
min_sdk="$(printf '%s\n' "$badging" | sed -n "s/^minSdkVersion:'\([0-9]*\)'.*/\1/p")"
target_sdk="$(printf '%s\n' "$badging" | sed -n "s/^targetSdkVersion:'\([0-9]*\)'.*/\1/p")"
if [ "${min_sdk:-0}" -ge 28 ]; then ok "minSdkVersion is $min_sdk"; else bad "minSdkVersion is '${min_sdk:-}', Qt 6.12 needs 28 or later"; fi
if [ "${target_sdk:-0}" -ge 35 ]; then ok "targetSdkVersion is $target_sdk"; else bad "targetSdkVersion is '${target_sdk:-}', Google Play wants 35 or later"; fi

# Permissions: the camera, and nothing else of the system's. The one other line AndroidX
# adds is the application's own signature-level permission for dynamic receivers, in the
# application's own name: it asks nothing of the person.
asked="$(printf '%s\n' "$permissions" | sed -n "s/^uses-permission: name='\(.*\)'.*/\1/p" | sort -u)"
unexpected=""
while IFS= read -r name; do
  [ -n "$name" ] || continue
  case "$name" in
    android.permission.CAMERA|"$application_id.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION") ;;
    *) unexpected="$unexpected $name" ;;
  esac
done <<<"$asked"
if [ -n "$unexpected" ]; then
  bad "the package asks for permissions beyond the camera:$unexpected (the manifest is libs/platforms/android/packaging/package-source/AndroidManifest.xml; Qt's core module adds INTERNET and storage permissions wherever its placeholder is)"
elif printf '%s\n' "$asked" | grep -qx 'android.permission.CAMERA'; then
  ok "the only permission asked of the person is the camera"
else
  bad "the package does not ask for the camera: Mode A could not start it"
fi

# The camera and the accelerometer are optional: a tablet without one still installs it.
for feature in android.hardware.camera android.hardware.sensor.accelerometer; do
  if printf '%s\n' "$badging" | grep -q "^[[:space:]]*uses-feature-not-required: name='$feature'"; then
    ok "$feature is not required"
  elif printf '%s\n' "$badging" | grep -q "name='$feature'"; then
    bad "$feature is required: a device without it could not install the program"
  fi
done

# One architecture, and the program's library is in it.
native="$(printf '%s\n' "$badging" | sed -n "s/^native-code: //p")"
if [ "$native" = "'arm64-v8a'" ]; then ok "native code is arm64-v8a only"; else bad "native code is $native, expected only 'arm64-v8a'"; fi
# The entries of the package, from whichever lister the machine has.
list_entries() {
  if command -v unzip >/dev/null 2>&1; then unzip -Z1 "$apk"
  elif command -v jar >/dev/null 2>&1; then jar tf "$apk"
  else python3 -c 'import sys, zipfile; print(chr(10).join(zipfile.ZipFile(sys.argv[1]).namelist()))' "$apk"
  fi
}
entries="$(list_entries | tr -d '\r')"
if printf '%s\n' "$entries" | grep -qx "$library"; then
  ok "the program's library $library is in the package"
else
  bad "the package has no $library"
fi

# Debuggable or not.
if printf '%s\n' "$badging" | grep -q "^application-debuggable"; then
  if [ "$release" = true ]; then bad "the package is debuggable; a release must not be"; else ok "the package is debuggable (a development build)"; fi
else
  ok "the package is not debuggable"
fi

# The icon and a launcher activity.
if printf '%s\n' "$badging" | grep -q "^application-icon-"; then ok "the package has a launcher icon"; else bad "the package has no launcher icon"; fi
if printf '%s\n' "$badging" | grep -q "^launchable-activity:"; then ok "the package has a launchable activity"; else bad "the package has no launchable activity"; fi

# The signature.
if [ "$signed" = true ] || [ "$release" = true ]; then
  certs="$("$apksigner" verify --print-certs "$apk" 2>&1 | tr -d '\r')" || { bad "apksigner does not verify the package: $(printf '%s\n' "$certs" | head -n 3 | tr '\n' ' ')"; certs=""; }
  if [ -n "$certs" ]; then
    ok "the signature verifies"
    if [ "$release" = true ] && printf '%s\n' "$certs" | grep -qi 'CN=Android Debug'; then
      bad "a release is signed with Android's debug key"
    elif [ "$release" = true ]; then
      ok "the signer is not Android's debug key"
    fi
  fi
fi

if [ "$status" -ne 0 ]; then
  echo "verify_apk.sh: $apk does not hold" >&2
  exit 1
fi
echo "verify_apk.sh: $apk holds"
