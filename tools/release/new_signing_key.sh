#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Create the release signing key pair, ON YOUR OWN MACHINE, and print the exact
# steps that put it to work. The repository's workflow never makes a key: it
# only reads one from two repository secrets (docs/ci.md, "Releases").
#
#   tools/release/new_signing_key.sh [--out <dir>] [--uid <name>] [--email <addr>]
#                                    [--expire <gpg expiry, default 2y>]
#                                    [--passphrase-file <file>] [--repo <owner/name>]
#                                    [--install]
#
# What it does:
#   1. makes a PRIVATE, temporary GnuPG home directory, so your own keyring is
#      never read or changed, and removes it when it finishes;
#   2. generates an Ed25519 signing-only key (default: expires in 2 years) under a
#      passphrase: --passphrase-file, or a random one it writes to the output;
#   3. writes, into --out (default ~/wjs-release-key, mode 700; never inside a git work tree):
#        private-key.asc     the secret key  -> repository secret GPG_PRIVATE_KEY
#        passphrase.txt      the passphrase  -> repository secret GPG_PASSPHRASE
#        public-key.asc      the public key (commit it as .github/release-signing/KEYS)
#        fingerprint.txt      the 40-hex fingerprint (commit it as .../FINGERPRINT)
#        revocation-cert.asc  the revocation certificate (keep it OFFLINE)
#   4. with --install, copies the public key and fingerprint to .github/release-signing/
#      in this checkout, ready to commit;
#   5. prints the gh commands that upload the two secrets, and what to delete.
#
# It does not upload anything, does not touch GitHub, and does not commit.
# Needs gpg (GnuPG 2.2 or later) and, to read the printed commands, nothing else.
set -euo pipefail

out="$HOME/wjs-release-key"
uid_name="WordJewelsSolver release signing key"
email=""
expire="2y"
pass_file=""
repo="iainchesworthlabs/wordjewelssolver"
install=false

while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="${2:?--out needs a value}"; shift 2 ;;
    --uid) uid_name="${2:?--uid needs a value}"; shift 2 ;;
    --email) email="${2:?--email needs a value}"; shift 2 ;;
    --expire) expire="${2:?--expire needs a value}"; shift 2 ;;
    --passphrase-file) pass_file="${2:?--passphrase-file needs a value}"; shift 2 ;;
    --repo) repo="${2:?--repo needs a value}"; shift 2 ;;
    --install) install=true; shift ;;
    -h|--help) sed -n '2,31p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "new_signing_key.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

command -v gpg >/dev/null 2>&1 || {
  echo "new_signing_key.sh: gpg is not on PATH. Install GnuPG (Git for Windows ships it in usr/bin; Gpg4win, or 'apt install gnupg', or 'brew install gnupg')." >&2
  exit 1
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null)" ]; then
  echo "new_signing_key.sh: $out already exists and is not empty. Refusing to overwrite a key; choose another --out." >&2
  exit 1
fi
umask 077
mkdir -p "$out"
out="$(cd "$out" && pwd)"
# Secrets must never be where a commit can pick them up.
if [[ "$out/" == "$repo_root"/* ]] || [ "$(git -C "$out" rev-parse --is-inside-work-tree 2>/dev/null)" = true ]; then
  rmdir "$out" 2>/dev/null || true
  echo "new_signing_key.sh: $out is inside a git working tree. Choose a folder that is not under version control (the default is ~/wjs-release-key)." >&2
  exit 1
fi

home="$(mktemp -d)"
cleanup() {
  gpgconf --homedir "$home" --kill all >/dev/null 2>&1 || true
  rm -rf "$home"
}
trap cleanup EXIT
chmod 700 "$home"

# Passphrase: the one given, or 32 random bytes in base64.
if [ -n "$pass_file" ]; then
  [ -s "$pass_file" ] || { echo "new_signing_key.sh: $pass_file is missing or empty." >&2; exit 1; }
  cp "$pass_file" "$out/passphrase.txt"
else
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -base64 32 | tr -d '\n' > "$out/passphrase.txt"
  else
    head -c 32 /dev/urandom | base64 | tr -d '\n' > "$out/passphrase.txt"
  fi
fi
chmod 600 "$out/passphrase.txt"

uid="$uid_name"
[ -n "$email" ] && uid="$uid_name <$email>"

g() { gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --yes --pinentry-mode loopback --passphrase-file "$out/passphrase.txt" "$@"; }

echo "==> generating an Ed25519 signing key for '$uid' (expires: $expire)"
g --quick-generate-key "$uid" ed25519 sign "$expire" >/dev/null 2>&1 || {
  echo "new_signing_key.sh: gpg could not generate the key (is this GnuPG 2.2 or later? gpg --version)." >&2
  exit 1
}
fpr="$(gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --with-colons --list-secret-keys | awk -F: '/^fpr:/ {print $10; exit}')"
[ -n "$fpr" ] || { echo "new_signing_key.sh: could not read the new key's fingerprint." >&2; exit 1; }

g --armor --export "$fpr" > "$out/public-key.asc"
g --armor --export-secret-keys "$fpr" > "$out/private-key.asc"
printf '%s\n' "$fpr" > "$out/fingerprint.txt"
# gpg writes a revocation certificate beside the key when it is generated.
rev="$home/openpgp-revocs.d/$fpr.rev"
if [ -f "$rev" ]; then
  cp "$rev" "$out/revocation-cert.asc"
else
  echo "warning: gpg wrote no revocation certificate; make one with 'gpg --gen-revoke' once the key is in your keyring." >&2
fi
chmod 600 "$out/private-key.asc" "$out/revocation-cert.asc" 2>/dev/null || true

# Prove the pair works before anyone depends on it.
probe="$home/probe.txt"
echo "probe" > "$probe"
g --armor --detach-sign --local-user "$fpr" --output "$probe.asc" "$probe"
gpg --quiet --no-auto-check-trustdb --homedir "$home" --batch --verify "$probe.asc" "$probe" >/dev/null 2>&1 || {
  echo "new_signing_key.sh: the new key failed a sign-and-verify round trip." >&2
  exit 1
}

if [ "$install" = true ]; then
  dest="$repo_root/.github/release-signing"
  mkdir -p "$dest"
  cp "$out/public-key.asc" "$dest/KEYS"
  cp "$out/fingerprint.txt" "$dest/FINGERPRINT"
  echo "==> installed the public key and fingerprint in $dest"
fi

winout="$out"
if command -v cygpath >/dev/null 2>&1; then winout="$(cygpath -w "$out")"; fi
pretty="$(printf '%s' "$fpr" | sed 's/.\{4\}/& /g; s/ $//')"
cat <<EOF

Done. A key pair was made in a temporary keyring (removed now) and written to:

  $out
  ($winout)

Fingerprint: $pretty
Expires:     $expire after today (put the renewal date in your calendar)

NEXT STEPS - in this order:

1. Upload the two secrets to the repository (gh must be logged in as an admin):

     gh secret set GPG_PRIVATE_KEY --repo $repo < "$out/private-key.asc"
     gh secret set GPG_PASSPHRASE  --repo $repo < "$out/passphrase.txt"

2. Keep a copy of private-key.asc and passphrase.txt in your password manager,
   and revocation-cert.asc somewhere OFFLINE. Then delete the files from disk:

     shred -u "$out/private-key.asc" "$out/passphrase.txt"   # or rm, if shred is missing

3. Commit the PUBLIC half (KEYS and FINGERPRINT in .github/release-signing/):
EOF
if [ "$install" = true ]; then
  echo "     already copied there by --install; review with: git status"
else
  cat <<EOF
     mkdir -p .github/release-signing
     cp "$out/public-key.asc" .github/release-signing/KEYS; cp "$out/fingerprint.txt" .github/release-signing/FINGERPRINT
EOF
fi
cat <<EOF
     git add .github/release-signing
   and publish the fingerprint where people will look for it - SECURITY.md,
   the README and the release page - so a download can be checked against
   something other than the download itself:

     $fpr

4. Rehearse with the real key before you tag, without publishing anything:

     gh workflow run release.yml --repo $repo -f signing=repository

   then fetch the 'release-dist' artifact of that run and verify it:

     tools/release/verify_release.sh --dir <the unpacked artifact>

The workflow stops, with this same list, if either secret is missing or if the
secret key's fingerprint differs from .github/release-signing/FINGERPRINT.
EOF
