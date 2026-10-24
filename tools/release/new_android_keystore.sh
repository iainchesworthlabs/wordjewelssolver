#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Create the Android UPLOAD keystore, ON YOUR OWN MACHINE, and print the exact steps
# that put it to work. The repository's workflow never makes a key: it only reads one
# from four repository secrets (docs/ci.md, "Android").
#
#   tools/release/new_android_keystore.sh [--out <dir>] [--alias <name>] [--dname <dn>]
#                                         [--validity-days <n>] [--repo <owner/name>]
#
# What it does:
#   1. generates an RSA 4096 key in a PKCS12 keystore with keytool (a JDK), valid for
#      --validity-days (default 9125, 25 years: Google Play wants a key that outlives
#      the app) under one random password. A PKCS12 store has one password for the
#      store and the key (keytool refuses a different key password), so
#      ANDROID_KEY_PASSWORD is the same value as ANDROID_KEYSTORE_PASSWORD;
#   2. writes, into --out (default ~/wjs-android-keystore, mode 700; never inside a git
#      work tree):
#        upload-keystore.p12         the keystore
#        upload-keystore.p12.base64  the same, base64 on one line -> ANDROID_KEYSTORE_BASE64
#        store-password.txt          -> ANDROID_KEYSTORE_PASSWORD
#        key-password.txt            -> ANDROID_KEY_PASSWORD (a copy of the store's)
#        key-alias.txt               -> ANDROID_KEY_ALIAS
#        certificate-sha256.txt      the certificate's SHA-256 fingerprint
#   3. proves it: lists the key back with the passwords it wrote;
#   4. prints the gh commands that upload the four secrets, and what to keep and delete.
#
# It does not upload anything, does not touch GitHub, does not commit, and refuses to
# overwrite a folder that already holds something. It is the UPLOAD key: Google Play
# re-signs what is uploaded with a key it holds (Play App Signing), and an upload key
# that is lost or leaked can be replaced by Google on request. A package signed with
# it and installed from GitHub is signed with this same key, so its loss ends updates
# to an installed copy; keep the keystore and both passwords in a password manager
# before deleting the files.
set -euo pipefail

out="$HOME/wjs-android-keystore"
alias_name="wjs-upload"
dname="CN=WordJewelsSolver upload key, O=Iain Chesworth Labs"
validity=9125
repo="iainchesworthlabs/wordjewelssolver"

while [ $# -gt 0 ]; do
  case "$1" in
    --out) out="${2:?--out needs a value}"; shift 2 ;;
    --alias) alias_name="${2:?--alias needs a value}"; shift 2 ;;
    --dname) dname="${2:?--dname needs a value}"; shift 2 ;;
    --validity-days) validity="${2:?--validity-days needs a value}"; shift 2 ;;
    --repo) repo="${2:?--repo needs a value}"; shift 2 ;;
    -h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "new_android_keystore.sh: unknown argument '$1'" >&2; exit 2 ;;
  esac
done

case "$alias_name" in
  *[!A-Za-z0-9._-]*|"") echo "new_android_keystore.sh: --alias may hold letters, digits, dot, dash and underscore only." >&2; exit 2 ;;
esac
case "$validity" in
  ''|*[!0-9]*) echo "new_android_keystore.sh: --validity-days wants a whole number." >&2; exit 2 ;;
esac

if ! command -v keytool >/dev/null 2>&1; then
  if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/keytool" ]; then PATH="$JAVA_HOME/bin:$PATH"; fi
fi
command -v keytool >/dev/null 2>&1 || {
  echo "new_android_keystore.sh: keytool is not on PATH. Install a JDK (17 or later) or set JAVA_HOME." >&2
  exit 1
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [ -e "$out" ] && [ -n "$(ls -A "$out" 2>/dev/null)" ]; then
  echo "new_android_keystore.sh: $out already exists and is not empty. Refusing to overwrite a key; choose another --out." >&2
  exit 1
fi
umask 077
mkdir -p "$out"
out="$(cd "$out" && pwd)"
# Secrets must never be where a commit can pick them up.
if [[ "$out/" == "$repo_root"/* ]] || [ "$(git -C "$out" rev-parse --is-inside-work-tree 2>/dev/null)" = true ]; then
  rmdir "$out" 2>/dev/null || true
  echo "new_android_keystore.sh: $out is inside a git working tree. Choose a folder that is not under version control (the default is ~/wjs-android-keystore)." >&2
  exit 1
fi

# A failed run leaves no half-made key behind.
done_ok=false
cleanup() { if [ "$done_ok" != true ]; then rm -rf "$out"; fi; }
trap cleanup EXIT

random_password() {
  # 24 characters of letters and digits: nothing a shell or a keytool prompt would mangle.
  if command -v openssl >/dev/null 2>&1; then
    openssl rand -base64 48 | tr -dc 'A-Za-z0-9' | head -c 24
  else
    head -c 96 /dev/urandom | base64 | tr -dc 'A-Za-z0-9' | head -c 24
  fi
}
store_password="$(random_password)"
if [ "${#store_password}" -ne 24 ]; then
  echo "new_android_keystore.sh: could not make a random password." >&2
  exit 1
fi
printf '%s' "$store_password" > "$out/store-password.txt"
printf '%s' "$store_password" > "$out/key-password.txt"
printf '%s' "$alias_name" > "$out/key-alias.txt"

keystore="$out/upload-keystore.p12"
echo "==> generating an RSA 4096 key '$alias_name' for '$dname' (valid $validity days)"
keytool -genkeypair -keystore "$keystore" -storetype PKCS12 \
  -alias "$alias_name" -keyalg RSA -keysize 4096 -validity "$validity" -dname "$dname" \
  -storepass:file "$out/store-password.txt" >/dev/null 2>&1 || {
  echo "new_android_keystore.sh: keytool could not generate the key (keytool -version; a JDK 17 or later is expected)." >&2
  exit 1
}

# Prove it: the password written is the one that opens the store (and so the key).
listing="$(keytool -J-Duser.language=en -list -v -keystore "$keystore" -storetype PKCS12 -storepass:file "$out/store-password.txt" -alias "$alias_name" 2>&1)" || {
  echo "new_android_keystore.sh: the keystore it made would not open with the password it wrote." >&2
  exit 1
}
sha256="$(printf '%s\n' "$listing" | sed -n 's/^[[:space:]]*SHA256:[[:space:]]*//p' | head -n 1 | tr -d '\r')"
if [ -z "$sha256" ]; then
  echo "new_android_keystore.sh: could not read the certificate's fingerprint." >&2
  exit 1
fi
printf '%s\n' "$sha256" > "$out/certificate-sha256.txt"

# One line, no wrapping: `gh secret set` stores exactly what it is given.
base64 < "$keystore" | tr -d '\n\r' > "$out/upload-keystore.p12.base64"
chmod 600 "$out"/* 2>/dev/null || true

winout="$out"
if command -v cygpath >/dev/null 2>&1; then winout="$(cygpath -w "$out")"; fi
done_ok=true
cat <<EOF

Done. The upload keystore was made, and opened again with the password written beside it:

  $out
  ($winout)

Certificate SHA-256: $sha256
Valid:               $validity days from today

NEXT STEPS - in this order:

1. Upload the four secrets to the repository (gh must be logged in as an admin):

     gh secret set ANDROID_KEYSTORE_BASE64   --repo $repo < "$out/upload-keystore.p12.base64"
     gh secret set ANDROID_KEYSTORE_PASSWORD --repo $repo < "$out/store-password.txt"
     gh secret set ANDROID_KEY_ALIAS         --repo $repo < "$out/key-alias.txt"
     gh secret set ANDROID_KEY_PASSWORD      --repo $repo < "$out/key-password.txt"

   Check they are there (names only, never values): gh secret list --repo $repo

2. Keep upload-keystore.p12, store-password.txt and key-password.txt in your password
   manager. Then delete the files from disk:

     shred -u "$out"/*   # or rm, if shred is missing

3. The first release.yml run after this signs the Android package with it; the
   release has no Android file until all four secrets exist (docs/ci.md, "Android").
   Google Play, later, takes this certificate as the upload key.
EOF
