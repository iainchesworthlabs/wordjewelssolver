#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Write SHA256SUMS for a release directory and sign what is in it with GPG.
#
#   tools/release/sign_release.sh --dist <dir> --keys-dir <.github/release-signing>
#                                 [--ephemeral]
#
# For every file in <dir> except the signatures it writes (a) SHA256SUMS, one
# "<sha256>  <name>" line per file in the byte order of the C locale, and (b) a
# detached, ASCII-armoured signature <file>.asc, SHA256SUMS included. Then it
# verifies every signature it made against the PUBLIC key that is committed in
# the repository, so a wrong or rotated secret cannot ship a release that the
# published key does not verify.
#
# The key comes from the environment, never from a file in the repository:
#
#   GPG_PRIVATE_KEY   the ASCII-armoured secret key (repository secret)
#   GPG_PASSPHRASE    its passphrase (repository secret)
#
# and must be the one named by <keys-dir>/FINGERPRINT. If either secret is empty
# the script stops with the steps that fix it (exit 3).
#
# --ephemeral signs with a throw-away key made for this run instead, for the
# pipeline's dry runs: it needs no secrets and proves the whole path, but the
# signatures it makes are NOT verifiable against the published key, and it says
# so, loudly. The dist directory then holds that run's public key as KEYS, so
# the dry run's output can be checked with verify_release.sh --dir <dist>.
#
# GnuPG runs in a private home directory removed on exit; the secret key never
# reaches the runner's own keyring or the log. Exit 1 = a check failed, 2 = bad
# invocation, 3 = the signing secrets are missing.
set -euo pipefail

dist=""
keys_dir=""
ephemeral=false

while [ $# -gt 0 ]; do
  case "$1" in
    --dist) dist="${2:?--dist needs a value}"; shift 2 ;;
    --keys-dir) keys_dir="${2:?--keys-dir needs a value}"; shift 2 ;;
    --ephemeral) ephemeral=true; shift ;;
    -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "sign_release.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done
[ -d "$dist" ] || { echo "sign_release.sh: --dist must name a directory" >&2; exit 2; }
command -v gpg >/dev/null 2>&1 || { echo "sign_release.sh: gpg is not on PATH" >&2; exit 1; }

annotate() {  # level message
  if [ -n "${GITHUB_ACTIONS:-}" ]; then echo "::$1::$2" >&2; else echo "$1: $2" >&2; fi
}

home="$(mktemp -d)"
chmod 700 "$home"
pass_file="$(mktemp)"
trap 'gpgconf --homedir "$home" --kill all >/dev/null 2>&1 || true; rm -rf "$home" "$pass_file"' EXIT

g() { gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --yes --pinentry-mode loopback --passphrase-file "$pass_file" "$@"; }

# --- the key -----------------------------------------------------------------
if [ "$ephemeral" = true ]; then
  annotate warning "EPHEMERAL signing key: this run's signatures prove the pipeline, not the release. They do not verify against the published key."
  head -c 24 /dev/urandom | base64 | tr -d '\n' > "$pass_file"
  g --quick-generate-key "WordJewelsSolver dry run (ephemeral)" ed25519 sign 1d >/dev/null 2>&1
  fpr="$(gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --with-colons --list-secret-keys | awk -F: '/^fpr:/ {print $10; exit}')"
  g --armor --export "$fpr" > "$dist/KEYS"
else
  [ -n "$keys_dir" ] || { echo "sign_release.sh: --keys-dir is required unless --ephemeral" >&2; exit 2; }
  if [ -z "${GPG_PRIVATE_KEY:-}" ] || [ -z "${GPG_PASSPHRASE:-}" ]; then
    annotate error "The release signing secrets are not set. Create the key on your own machine and upload it: run tools/release/new_signing_key.sh (or .ps1), then 'gh secret set GPG_PRIVATE_KEY' and 'gh secret set GPG_PASSPHRASE' as it prints, and commit .github/release-signing/KEYS and FINGERPRINT. To test the pipeline without a key, dispatch release.yml with signing=ephemeral. See docs/ci.md, 'Releases'."
    exit 3
  fi
  [ -s "$keys_dir/FINGERPRINT" ] && [ -s "$keys_dir/KEYS" ] || {
    annotate error "$keys_dir/KEYS and $keys_dir/FINGERPRINT are not committed. new_signing_key.sh --install writes them; commit them, then run the release again."
    exit 3
  }
  expected="$(tr -d '[:space:]' < "$keys_dir/FINGERPRINT" | tr '[:lower:]' '[:upper:]')"
  printf '%s' "$GPG_PASSPHRASE" > "$pass_file"
  if ! printf '%s\n' "$GPG_PRIVATE_KEY" | g --import >/dev/null 2>&1; then
    annotate error "GPG_PRIVATE_KEY could not be imported with GPG_PASSPHRASE. It must be the ASCII-armoured output of 'gpg --export-secret-keys --armor' (private-key.asc from new_signing_key.sh), and the passphrase the one it was made with."
    exit 3
  fi
  fpr="$(gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --with-colons --list-secret-keys | awk -F: '/^fpr:/ {print $10; exit}')"
  if [ "$fpr" != "$expected" ]; then
    annotate error "The secret key's fingerprint is $fpr but $keys_dir/FINGERPRINT says $expected. Upload the right key as GPG_PRIVATE_KEY, or commit the matching public key and fingerprint."
    exit 3
  fi
  # Importing a protected key does not check the passphrase; signing does.
  echo probe > "$home/probe.txt"
  if ! g --armor --detach-sign --local-user "$fpr" --output "$home/probe.asc" "$home/probe.txt" >/dev/null 2>&1; then
    annotate error "GPG_PASSPHRASE does not unlock the key $fpr. Upload the passphrase the key was made with (passphrase.txt from new_signing_key.sh) as GPG_PASSPHRASE."
    exit 3
  fi
  # A key past its expiry signs, and then no one can verify the signature.
  expiry="$(gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --with-colons --list-secret-keys | awk -F: '/^sec:/ {print $7; exit}')"
  if [ -n "$expiry" ] && [ "$expiry" -le "$(date +%s)" ]; then
    annotate error "The signing key $fpr expired on $(date -u -d "@$expiry" +%F 2>/dev/null || echo "$expiry"). Extend or replace it (docs/ci.md, 'Releases') before tagging."
    exit 3
  fi
  cp "$keys_dir/KEYS" "$dist/KEYS"
fi

# --- checksums, then signatures ----------------------------------------------
(
  cd "$dist"
  rm -f SHA256SUMS ./*.asc
  # Everything except KEYS (a public key proves nothing about itself) in the
  # byte order of the C locale, so the file is the same on every machine.
  # (A shell glob, not find -printf and not ls: the BSD tools on macOS lack the
  # first, and a glob needs no parsing. sort -z is avoided for the same reason.)
  : > "$home/files.txt"
  for candidate in ./*; do
    [ -f "$candidate" ] || continue
    name="${candidate#./}"
    case "$name" in KEYS|SHA256SUMS) continue ;; esac
    printf '%s
' "$name" >> "$home/files.txt"
  done
  LC_ALL=C sort -o "$home/files.txt" "$home/files.txt"
  [ -s "$home/files.txt" ] || { echo "sign_release.sh: nothing to sign in $dist" >&2; exit 1; }
  : > SHA256SUMS
  while IFS= read -r name; do
    # "<hash>  <name>", from sha256sum where there is one and shasum on macOS.
    # (sed: sha256sum marks a binary-mode name with " *" on some builds; the file is the same everywhere.)
    if command -v sha256sum >/dev/null 2>&1; then sha256sum -- "$name"; else shasum -a 256 -- "$name"; fi | sed 's/ \*/  /' >> SHA256SUMS
  done < "$home/files.txt"
  echo "SHA256SUMS:"; sed 's/^/  /' SHA256SUMS
)

signed=0
for file in "$dist"/*; do
  name="$(basename "$file")"
  case "$name" in *.asc|KEYS) continue ;; esac
  g --armor --detach-sign --local-user "$fpr" --output "$file.asc" "$file"
  signed=$((signed + 1))
done
echo "signed $signed file(s) with key $fpr"

# --- the signatures must verify against the PUBLIC key that is published ------
verify_home="$(mktemp -d)"
chmod 700 "$verify_home"
trap 'gpgconf --homedir "$home" --kill all >/dev/null 2>&1 || true; rm -rf "$home" "$pass_file" "$verify_home"' EXIT
gpg --quiet --no-auto-check-trustdb --homedir "$verify_home" --batch --import "$dist/KEYS" >/dev/null 2>&1
for sig in "$dist"/*.asc; do
  if ! gpg --quiet --no-auto-check-trustdb --homedir "$verify_home" --batch --status-fd 1 --verify "$sig" "${sig%.asc}" 2>/dev/null | grep -q "^\[GNUPG:\] VALIDSIG $fpr "; then
    annotate error "$(basename "$sig") does not verify against $dist/KEYS."
    exit 1
  fi
done
echo "every signature verifies against KEYS (fingerprint $fpr)"
