#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Open a release package the way a user would and run what is in it.
#
#   tools/release/smoke_package.sh <package> [<package> ...]
#
# Understands .zip, .tar.gz, .deb, .rpm and .dmg. For each it unpacks into a
# temporary directory (a disk image is mounted read-only), finds wjs-cli, and
# solves a board with the dictionary the package installed beside it - no -d, so
# a missing or stale dictionaries/en.txt is a failure and not a silent fallback.
# CAT, CARE and TAR must be found on CAT / ORE / DGS. The Windows ZIP has its
# own, fuller check (check_package.ps1); this is the check for the rest.
#
# Exit 0 = every package passed; 1 = one failed; 2 = bad invocation.
set -euo pipefail

[ $# -gt 0 ] || { echo "usage: smoke_package.sh <package> [<package> ...]" >&2; exit 2; }

fail() { echo "FAILED: $*" >&2; exit 1; }

check_tree() {  # directory
  local root="$1" cli
  cli="$(find "$root" -type f \( -name wjs-cli -o -name wjs-cli.exe \) | head -n 1)"
  [ -n "$cli" ] || fail "no wjs-cli in the unpacked package ($root)"
  chmod +x "$cli" 2>/dev/null || true
  local bin_dir
  bin_dir="$(dirname "$cli")"
  [ -f "$bin_dir/dictionaries/en.txt" ] || fail "no dictionaries/en.txt beside $(basename "$cli")"
  [ -f "$bin_dir/NOTICES.txt" ] || fail "no NOTICES.txt beside $(basename "$cli")"
  [ -f "$bin_dir/LICENSE.txt" ] || fail "no LICENSE.txt beside $(basename "$cli")"
  local out
  out="$("$cli" CAT ORE DGS 2>&1)" || fail "wjs-cli exited non-zero: $out"
  for word in CAT CARE TAR; do
    printf '%s\n' "$out" | grep -Eq "^${word}[[:space:]]" || fail "wjs-cli did not find $word using the packaged dictionary"
  done
  echo "    ok: $(basename "$cli") solved a board with its own dictionary"
}

for package in "$@"; do
  [ -f "$package" ] || fail "$package does not exist"
  name="$(basename "$package")"
  echo "==> $name"
  work="$(mktemp -d)"
  mount_point=""
  cleanup() {
    [ -n "$mount_point" ] && hdiutil detach "$mount_point" -quiet >/dev/null 2>&1 || true
    rm -rf "$work"
  }
  trap cleanup EXIT
  case "$name" in
    *.zip)
      if command -v unzip >/dev/null 2>&1; then unzip -q "$package" -d "$work"
      else python3 -m zipfile -e "$package" "$work"; fi
      check_tree "$work" ;;
    *.tar.gz|*.tgz)
      tar -xzf "$package" -C "$work"
      check_tree "$work" ;;
    *.deb)
      command -v dpkg-deb >/dev/null 2>&1 || fail "dpkg-deb is needed to open $name"
      dpkg-deb -x "$package" "$work"
      check_tree "$work" ;;
    *.rpm)
      { command -v rpm2cpio >/dev/null 2>&1 && command -v cpio >/dev/null 2>&1; } || fail "rpm2cpio and cpio are needed to open $name"
      (cd "$work" && rpm2cpio "$package" | cpio -idm --quiet)
      check_tree "$work" ;;
    *.dmg)
      command -v hdiutil >/dev/null 2>&1 || fail "hdiutil is needed to open $name (macOS only)"
      mount_point="$work/mnt"
      mkdir "$mount_point"
      hdiutil attach -nobrowse -readonly -mountpoint "$mount_point" "$package" -quiet
      check_tree "$mount_point" ;;
    *) fail "do not know how to open $name" ;;
  esac
  cleanup
  trap - EXIT
done
echo "OK: $# package(s) smoke-tested"
