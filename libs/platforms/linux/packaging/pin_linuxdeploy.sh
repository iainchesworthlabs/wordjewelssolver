#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Record the SHA-256 of the AppImage tools in linuxdeploy.pins.
#
#   libs/platforms/linux/packaging/pin_linuxdeploy.sh
#
# Downloads each file named in linuxdeploy.pins from its pinned release URL
# (about 30 MB in all), computes its SHA-256 and rewrites the file with the hashes.
# Run it once, on a machine you trust, and commit the result; then
# build_appimage.sh refuses any download that differs. It overwrites an existing
# hash, so run it only after changing a url on purpose.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pins="$here/linuxdeploy.pins"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

: > "$work/new.pins"
while IFS= read -r line; do
  case "$line" in
    ''|'#'*) printf '%s\n' "$line" >> "$work/new.pins"; continue ;;
  esac
  read -r name url _ <<<"$line"
  echo "downloading $name" >&2
  curl --fail --silent --show-error --location --output "$work/$name" "$url"
  hash="$(sha256sum "$work/$name" | cut -d' ' -f1)"
  echo "  $hash" >&2
  printf '%s %s %s\n' "$name" "$url" "$hash" >> "$work/new.pins"
done < "$pins"

cp "$work/new.pins" "$pins"
echo "recorded in $pins - review with git diff, then commit" >&2
