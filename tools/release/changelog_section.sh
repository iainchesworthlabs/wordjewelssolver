#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Print one version's section of CHANGELOG.md (the release notes).
#
#   tools/release/changelog_section.sh 0.1.0 [CHANGELOG.md]
#
# The section runs from its "## [0.1.0]" heading up to, not including, the next
# "## [" heading, trimmed of blank lines at both ends. Exit 1 when there is no
# such section, 2 on a bad invocation.
set -euo pipefail

version="${1:-}"
file="${2:-CHANGELOG.md}"
[ -n "$version" ] || { echo "usage: changelog_section.sh <version> [CHANGELOG.md]" >&2; exit 2; }
[ -f "$file" ] || { echo "changelog_section.sh: $file does not exist" >&2; exit 2; }

section="$(tr -d '\r' < "$file" | awk -v v="$version" '
  BEGIN { heading = "## [" v "]" }
  index($0, heading) == 1 { found = 1; next }
  found && /^## \[/ { exit }
  found { print }
  END { if (!found) exit 3 }
' || true)"
status=0
tr -d '\r' < "$file" | grep -Fq "## [$version]" || status=1
if [ "$status" -ne 0 ]; then
  echo "changelog_section.sh: no '## [$version]' section in $file" >&2
  exit 1
fi
# Trim leading and trailing blank lines.
printf '%s\n' "$section" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}'
