#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Sign the macOS disk image's application with a Developer ID, notarise the image
# with Apple and staple the ticket - or, with no signing secrets, do nothing and say
# so.
#
#   libs/platforms/macos/packaging/sign_and_notarize.sh <package.dmg>
#
# The secrets come from the environment (repository secrets, in the release
# workflow). All of them are needed to sign; none, to skip:
#
#   MACOS_CERTIFICATE            base64 of the Developer ID Application certificate
#                                with its private key, as a .p12
#   MACOS_CERTIFICATE_PASSWORD   the .p12's password
#   MACOS_SIGN_IDENTITY          e.g. "Developer ID Application: Name (TEAMID)"
#   NOTARY_APPLE_ID              the Apple ID that notarises
#   NOTARY_TEAM_ID               its team id
#   NOTARY_PASSWORD              an app-specific password for that Apple ID
#
# With the secrets the disk image is replaced by one whose application, frameworks,
# plugins and wjs-cli are signed with the hardened runtime (Entitlements.plist),
# whose own signature is valid, and whose notarisation Apple accepted and is
# stapled. A partial set of secrets is an error, not a quiet skip: that is a
# release someone meant to sign.
#
# The signing keychain is temporary and deleted on exit. macOS only. Unverified
# until a run with real secrets: this repository has none.
set -euo pipefail

dmg="${1:-}"
[ -f "$dmg" ] || { echo "usage: sign_and_notarize.sh <package.dmg>" >&2; exit 2; }
dmg="$(cd "$(dirname "$dmg")" && pwd)/$(basename "$dmg")"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

names=(MACOS_CERTIFICATE MACOS_CERTIFICATE_PASSWORD MACOS_SIGN_IDENTITY NOTARY_APPLE_ID NOTARY_TEAM_ID NOTARY_PASSWORD)
set_count=0
missing=()
for name in "${names[@]}"; do
  if [ -n "${!name:-}" ]; then set_count=$((set_count + 1)); else missing+=("$name"); fi
done
if [ "$set_count" -eq 0 ]; then
  echo "::notice title=macOS package not signed::No macOS signing secrets are set, so $(basename "$dmg") is unsigned and not notarised: Gatekeeper will warn on first run. See docs/ci.md, 'Releases'."
  exit 0
fi
if [ "$set_count" -ne "${#names[@]}" ]; then
  echo "::error title=macOS signing secrets incomplete::Set all of ${names[*]} or none. Missing: ${missing[*]}." >&2
  exit 1
fi

work="$(mktemp -d)"
keychain="$work/signing.keychain-db"
keychain_password="$(uuidgen)"
mount_point=""
cleanup() {
  if [ -n "$mount_point" ]; then hdiutil detach "$mount_point" -quiet >/dev/null 2>&1 || true; fi
  security delete-keychain "$keychain" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

# --- a temporary keychain holding the certificate -------------------------------
printf '%s' "$MACOS_CERTIFICATE" | base64 --decode > "$work/certificate.p12"
security create-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 3600 "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security import "$work/certificate.p12" -k "$keychain" -P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign -T /usr/bin/security
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychain_password" "$keychain" >/dev/null
existing=()
while IFS= read -r entry; do existing+=("$entry"); done < <(security list-keychains -d user | sed -e 's/^[[:space:]]*"//' -e 's/"$//')
security list-keychains -d user -s "$keychain" "${existing[@]}"
rm -f "$work/certificate.p12"

# --- the image's contents, copied out ---------------------------------------------
mount_point="$work/mnt"
mkdir "$mount_point"
hdiutil attach -nobrowse -readonly -mountpoint "$mount_point" "$dmg" -quiet
staging="$work/staging"
mkdir "$staging"
ditto "$mount_point" "$staging"
hdiutil detach "$mount_point" -quiet
mount_point=""

sign() {  # path...
  codesign --force --timestamp --options runtime --entitlements "$here/Entitlements.plist" \
    --keychain "$keychain" --sign "$MACOS_SIGN_IDENTITY" "$@"
}

# --- sign, inside out ------------------------------------------------------------
app="$(find "$staging" -maxdepth 2 -name '*.app' -type d | head -n 1)"
[ -n "$app" ] || { echo "sign_and_notarize.sh: no .app in $dmg" >&2; exit 1; }
# Loose Mach-O files first (dylibs, plugins, helper programs, wjs-cli), then
# frameworks, then the bundle: a signature covers what is inside.
while IFS= read -r file; do
  if file "$file" | grep -q 'Mach-O'; then sign "$file"; fi
done < <(find "$staging" -type f ! -path '*.framework/*' ! -path "$app/Contents/MacOS/*")
while IFS= read -r framework; do
  sign "$framework"
done < <(find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' -type d 2>/dev/null)
sign "$app/Contents/MacOS/WordJewelsSolver"
sign "$app"
codesign --verify --deep --strict --verbose=2 "$app"

# --- a new image, signed ---------------------------------------------------------
volume="$(basename "$dmg" .dmg)"
signed="$work/signed.dmg"
hdiutil create -volname "$volume" -srcfolder "$staging" -ov -format UDZO "$signed" -quiet
codesign --force --timestamp --keychain "$keychain" --sign "$MACOS_SIGN_IDENTITY" "$signed"

# --- notarise and staple ---------------------------------------------------------
echo "==> submitting $volume to Apple's notary service (this can take minutes)"
if ! xcrun notarytool submit "$signed" --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
    --password "$NOTARY_PASSWORD" --wait --output-format json > "$work/notary.json"; then
  cat "$work/notary.json" >&2 || true
  echo "sign_and_notarize.sh: the notary service did not accept the image" >&2
  exit 1
fi
status="$(plutil -extract status raw -o - "$work/notary.json" 2>/dev/null || true)"
if [ "$status" != "Accepted" ]; then
  cat "$work/notary.json" >&2
  echo "sign_and_notarize.sh: notarisation finished as '$status', not Accepted" >&2
  exit 1
fi
xcrun stapler staple "$signed"
spctl --assess --type open --context context:primary-signature --verbose=2 "$signed"

mv -f "$signed" "$dmg"
echo "==> $(basename "$dmg") is signed, notarised and stapled"
