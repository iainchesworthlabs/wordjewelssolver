#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Decide whether a ref may be released, and say what it releases.
#
#   tools/release/check_release_ref.sh --tag v0.1.0 [--sha <commit>]
#                                      [--main-ref origin/main] [--root <dir>]
#   tools/release/check_release_ref.sh --dry-run <label> [--root <dir>]
#
# The checks, each with its own message:
#
#   1. the tag is vMAJOR.MINOR.PATCH, or the same with a -prerelease suffix
#      (v0.1.0-rc.1): anything else matched by the workflow's v* filter stops here;
#   2. MAJOR.MINOR.PATCH is the version CMakeLists.txt's project() declares and
#      vcpkg.json's "version": the packages take their name and version from the
#      first, so a tag that disagrees would ship a v0.2.0 release that contains
#      0.1.0 files;
#   3. a final release has a "## [MAJOR.MINOR.PATCH]" section in CHANGELOG.md
#      (the release notes are that section); a prerelease may fall back to
#      generated notes;
#   4. a final release is a commit on --main-ref (default origin/main). A
#      prerelease may be cut from a release/* branch before it is merged.
#
# --dry-run <label> is for a run that publishes nothing (a manual or scheduled
# rehearsal): there is no tag, so checks 1, 3 and 4 do not apply; check 2 still
# does, and the version is taken from the tree. The stand-in tag it reports is
# v<version>-<label>, which is a valid prerelease tag and names that run's
# files and SBOM.
#
# On success it prints version=, tag=, prerelease= and suffix= lines (suffix is the
# part after the hyphen, empty for a final release) (also appended to
# $GITHUB_OUTPUT when that is set) and exits 0. Exit 1 = a check failed (the
# message says which and how to fix it); exit 2 = bad invocation.
set -euo pipefail

tag=""
dry_run=""
sha="HEAD"
main_ref="origin/main"
root="."

while [ $# -gt 0 ]; do
  case "$1" in
    --tag) tag="${2:?--tag needs a value}"; shift 2 ;;
    --dry-run) dry_run="${2:?--dry-run needs a label}"; shift 2 ;;
    --sha) sha="${2:?--sha needs a value}"; shift 2 ;;
    --main-ref) main_ref="${2:?--main-ref needs a value}"; shift 2 ;;
    --root) root="${2:?--root needs a value}"; shift 2 ;;
    -h|--help) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "check_release_ref.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done
if [ -z "$tag" ] && [ -z "$dry_run" ]; then
  echo "check_release_ref.sh: --tag (or --dry-run <label>) is required" >&2
  exit 2
fi
if [ -n "$tag" ] && [ -n "$dry_run" ]; then
  echo "check_release_ref.sh: --tag and --dry-run are alternatives" >&2
  exit 2
fi

fail() {
  if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::error::$*" >&2; else echo "FAILED: $*" >&2; fi
  exit 1
}

# A dry run's tag is made from the tree's own version.
if [ -n "$dry_run" ]; then
  [[ "$dry_run" =~ ^[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*$ ]] || fail "the dry-run label '$dry_run' is not a valid prerelease identifier."
  dry_version="$(tr -d '\r' < "$root/CMakeLists.txt" | awk '
    /^project\(WordJewelsSolver/ { inproject = 1 }
    inproject && $1 == "VERSION" { print $2; exit }')"
  [ -n "$dry_version" ] || fail "no 'project(WordJewelsSolver ... VERSION x.y.z' found in $root/CMakeLists.txt."
  tag="v${dry_version}-${dry_run}"
fi

# 1. The tag's shape. Numeric parts without leading zeros, as SemVer says.
num='(0|[1-9][0-9]*)'
pre='[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*'
re="^v(${num}\.${num}\.${num})(-(${pre}))?$"
if ! [[ "$tag" =~ $re ]]; then
  fail "the tag '$tag' is not vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-prerelease (for example v0.1.0 or v0.1.0-rc.1)."
fi
version="${BASH_REMATCH[1]}"
prerelease=false
suffix="${BASH_REMATCH[6]:-}"
[ -n "$suffix" ] && prerelease=true

# 2. The tag against the tree's own version.
cmake_file="$root/CMakeLists.txt"
[ -f "$cmake_file" ] || fail "$cmake_file does not exist; run from a checkout or pass --root."
cmake_version="$(tr -d '\r' < "$cmake_file" | awk '
  /^project\(WordJewelsSolver/ { inproject = 1 }
  inproject && $1 == "VERSION" { print $2; exit }')"
[ -n "$cmake_version" ] || fail "no 'project(WordJewelsSolver ... VERSION x.y.z' found in $cmake_file."
if [ "$cmake_version" != "$version" ]; then
  fail "the tag $tag is version $version but CMakeLists.txt declares $cmake_version. Change project(VERSION) and vcpkg.json in a commit, merge it, and tag that commit."
fi
manifest_version="$(tr -d '\r' < "$root/vcpkg.json" | sed -n 's/^[[:space:]]*"version":[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
if [ "$manifest_version" != "$version" ]; then
  fail "the tag $tag is version $version but vcpkg.json's version is '$manifest_version'."
fi

# 3. The release notes.
changelog="$root/CHANGELOG.md"
if [ -n "$dry_run" ]; then
  :
elif tr -d '\r' < "$changelog" | grep -Eq "^## \[$(printf '%s' "$version" | sed 's/\./\\./g')\]"; then
  :
elif [ "$prerelease" = true ]; then
  echo "note: CHANGELOG.md has no '## [$version]' section; a prerelease gets generated notes." >&2
else
  fail "CHANGELOG.md has no '## [$version]' section. Rename [Unreleased] to [$version] - <date> (keeping a fresh [Unreleased] above it) in the release pull request."
fi

# 4. A final release is main's.
if [ "$prerelease" = false ] && [ -z "$dry_run" ]; then
  if ! git -C "$root" rev-parse --verify --quiet "$main_ref^{commit}" >/dev/null; then
    fail "cannot find $main_ref to check that $tag is on main (a shallow checkout? fetch with full history)."
  fi
  if ! git -C "$root" merge-base --is-ancestor "$sha" "$main_ref"; then
    fail "the commit $sha is not on $main_ref. A final release is tagged on main after the release pull request is merged; cut a -rc tag to try a branch."
  fi
fi

echo "release ref ok: $tag -> version $version (prerelease=$prerelease)"
{
  echo "version=$version"
  echo "tag=$tag"
  echo "prerelease=$prerelease"
  echo "suffix=$suffix"
} | tee -a "${GITHUB_OUTPUT:-/dev/null}"
