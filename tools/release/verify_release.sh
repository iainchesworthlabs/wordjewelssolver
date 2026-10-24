#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Verify a WordJewelsSolver release: the signing key, the signatures, the
# checksums and (optionally) the build-provenance attestations.
#
#   tools/release/verify_release.sh --dir <folder holding the downloaded files>
#                                   [--fingerprint <40 hex>] [--keys <KEYS file>]
#                                   [--attest --repo <owner/name>] [--unpinned]
#   tools/release/verify_release.sh --repo <owner/name> --tag <vX.Y.Z> [...]
#       (downloads the release's files with the GitHub CLI into a temp folder)
#
# What it checks, in order, and stops at the first failure:
#
#   1. the key in KEYS has the fingerprint you expect. That is the step that
#      makes the rest mean something: a signature checked against a key that
#      came from the same place as the download only proves the download is
#      self-consistent. The fingerprint comes from --fingerprint, else from
#      .github/release-signing/FINGERPRINT in a checkout of this repository, and
#      should match the one in SECURITY.md and on the release page. --unpinned
#      skips this step and says so (for a dry run's ephemeral key);
#   2. SHA256SUMS.asc is a good signature by that key over SHA256SUMS, and so is
#      every other <file>.asc over its file;
#   3. sha256sum -c: every file listed in SHA256SUMS has its recorded hash;
#   4. with --attest: gh attestation verify for every listed file, proving the
#      GitHub Actions workflow of this repository built it (needs gh >= 2.49).
#
# GnuPG runs in a private temporary home; your keyring is not touched.
# Exit 0 = everything checked passed; 1 = a check failed; 2 = bad invocation.
set -euo pipefail

dir=""
fingerprint=""
keys=""
repo=""
tag=""
attest=false
unpinned=false

while [ $# -gt 0 ]; do
  case "$1" in
    --dir) dir="${2:?--dir needs a value}"; shift 2 ;;
    --fingerprint) fingerprint="${2:?--fingerprint needs a value}"; shift 2 ;;
    --keys) keys="${2:?--keys needs a value}"; shift 2 ;;
    --repo) repo="${2:?--repo needs a value}"; shift 2 ;;
    --tag) tag="${2:?--tag needs a value}"; shift 2 ;;
    --attest) attest=true; shift ;;
    --unpinned) unpinned=true; shift ;;
    -h|--help) sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "verify_release.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

fail() { echo "FAILED: $*" >&2; exit 1; }
command -v gpg >/dev/null 2>&1 || fail "gpg is not on PATH (install GnuPG)."

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
chmod 700 "$tmp"
trap 'gpgconf --homedir "$tmp/gnupg" --kill all >/dev/null 2>&1 || true; rm -rf "$tmp"' EXIT

if [ -z "$dir" ]; then
  [ -n "$repo" ] && [ -n "$tag" ] || { echo "verify_release.sh: give --dir, or --repo and --tag" >&2; exit 2; }
  command -v gh >/dev/null 2>&1 || fail "--repo/--tag need the GitHub CLI (gh) to download the release."
  dir="$tmp/release"
  mkdir -p "$dir"
  echo "==> downloading $repo $tag into $dir"
  gh release download "$tag" --repo "$repo" --dir "$dir"
fi
[ -d "$dir" ] || { echo "verify_release.sh: $dir is not a directory" >&2; exit 2; }
dir="$(cd "$dir" && pwd)"
[ -f "$dir/SHA256SUMS" ] || fail "$dir/SHA256SUMS is missing."
[ -f "$dir/SHA256SUMS.asc" ] || fail "$dir/SHA256SUMS.asc is missing."
[ -n "$keys" ] || keys="$dir/KEYS"
[ -f "$keys" ] || fail "no public key: $keys does not exist (pass --keys)."

# 1. The key.
mkdir "$tmp/gnupg"; chmod 700 "$tmp/gnupg"
gpg() { command gpg --quiet --no-auto-check-trustdb --homedir "$tmp/gnupg" --batch "$@"; }
gpg --import "$keys" >/dev/null 2>&1 || fail "$keys is not a public key block gpg can import."
actual="$(gpg --with-colons --list-keys | awk -F: '/^fpr:/ {print $10; exit}')"
[ -n "$actual" ] || fail "no key was imported from $keys."
if [ -z "$fingerprint" ] && [ -f "$here/../../.github/release-signing/FINGERPRINT" ]; then
  fingerprint="$(cat "$here/../../.github/release-signing/FINGERPRINT")"
  echo "==> expected fingerprint read from the checkout's .github/release-signing/FINGERPRINT"
fi
fingerprint="$(printf '%s' "$fingerprint" | tr -d '[:space:]' | tr '[:lower:]' '[:upper:]')"
if [ "$unpinned" = true ]; then
  echo "WARNING: --unpinned: the key's fingerprint ($actual) was not compared with anything. This proves the files are consistent with $keys, nothing more." >&2
elif [ -z "$fingerprint" ]; then
  fail "no expected fingerprint. Pass --fingerprint <40 hex> (the one in SECURITY.md and on the release page), or --unpinned to skip this step knowingly."
elif [ "$actual" != "$fingerprint" ]; then
  fail "the key in $keys has fingerprint $actual, not the expected $fingerprint. Do not trust these files."
else
  echo "==> key $actual matches the expected fingerprint"
fi

# 2. The signatures.
count=0
for sig in "$dir"/*.asc; do
  file="${sig%.asc}"
  [ -f "$file" ] || fail "$(basename "$sig") has no file $(basename "$file") beside it."
  status="$(gpg --status-fd 1 --verify "$sig" "$file" 2>/dev/null || true)"
  printf '%s\n' "$status" | grep -q "^\[GNUPG:\] VALIDSIG $actual " \
    || fail "$(basename "$sig") is not a valid signature by $actual over $(basename "$file")."
  count=$((count + 1))
done
[ "$count" -gt 0 ] || fail "no .asc signatures in $dir."
echo "==> $count signature(s) verified"

# 3. The checksums.
(
  cd "$dir"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum -c SHA256SUMS
  else
    shasum -a 256 -c SHA256SUMS
  fi
) || fail "a file does not match SHA256SUMS."

# 4. The attestations.
if [ "$attest" = true ]; then
  [ -n "$repo" ] || fail "--attest needs --repo <owner/name>."
  command -v gh >/dev/null 2>&1 || fail "--attest needs the GitHub CLI (gh)."
  while read -r _ name; do
    gh attestation verify "$dir/$name" --repo "$repo" \
      --signer-workflow "$repo/.github/workflows/release.yml" >/dev/null \
      || fail "no valid build-provenance attestation from $repo's release workflow for $name."
    echo "    attested: $name"
  done < "$dir/SHA256SUMS"
  echo "==> attestations verified"
fi

echo "OK: $(basename "$dir") verified."
