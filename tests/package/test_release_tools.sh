#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Self-test of the release tooling (tools/release) and of the release workflow's
# pins, with no build and no network: the release pipeline is mostly scripts, and
# a script that has only ever run on a tag day is a script that fails on one.
#
#   tests/package/test_release_tools.sh
#
# Covers: the tag/version/changelog/main checks, the changelog section, signing
# and verification round trips with a throw-away key (good, tampered, wrong
# fingerprint, missing and wrong secrets), the SBOM's structure, and that
# release.yml stays within the repository's workflow rules (pinned actions,
# toolchain pins equal to .github/toolchain-versions.json, no Tracy on, no
# ${{ }} in a run block). Run in CI by .github/workflows/release-tooling.yml.
#
# Needs bash, gpg, git, jq and pwsh (7+). Everything it makes is in a temporary
# directory, including every key; nothing leaves the machine.
# shellcheck disable=SC2015,SC2012  # `A && ok || bad` is deliberate: ok cannot fail
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tools="$repo/tools/release"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

failures=0
checks=0
ok() { checks=$((checks + 1)); echo "  ok    $1"; }
bad() { checks=$((checks + 1)); failures=$((failures + 1)); echo "  FAIL  $1" >&2; }
expect_status() {  # description expected-status command...
  local description="$1" expected="$2"; shift 2
  local status=0
  "$@" >"$work/out.txt" 2>&1 || status=$?
  if [ "$status" -eq "$expected" ]; then ok "$description"; else bad "$description (exit $status, expected $expected): $(head -c 300 "$work/out.txt")"; fi
}
expect_output() {  # description pattern  (against the last command's output)
  if grep -Eq -- "$2" "$work/out.txt"; then ok "$1"; else bad "$1 (no match for '$2' in: $(head -c 300 "$work/out.txt"))"; fi
}

for tool in gpg git jq pwsh; do
  command -v "$tool" >/dev/null 2>&1 || { echo "test_release_tools.sh: $tool is needed" >&2; exit 2; }
done

# --- a miniature repository for the ref checks --------------------------------
echo "check_release_ref.sh"
fixture="$work/fixture"
mkdir -p "$fixture"
printf 'project(WordJewelsSolver\n    VERSION 1.2.3\n    LANGUAGES CXX)\n' > "$fixture/CMakeLists.txt"
printf '{\n  "name": "x",\n  "version": "1.2.3"\n}\n' > "$fixture/vcpkg.json"
printf '# Changelog\n\n## [Unreleased]\n\n- next\n\n## [1.2.3] - 2026-01-01\n\n### Added\n\n- a thing\n\n## [1.2.2]\n\n- older\n' > "$fixture/CHANGELOG.md"
(
  cd "$fixture" || exit 1
  git init -q -b main . && git -c user.name=t -c user.email=t@example.invalid add -A \
    && git -c user.name=t -c user.email=t@example.invalid commit -q -m "a"
) >/dev/null 2>&1
main_sha="$(git -C "$fixture" rev-parse HEAD)"
git -C "$fixture" checkout -q -b side
printf 'x\n' > "$fixture/side.txt"
git -C "$fixture" add side.txt
git -C "$fixture" -c user.name=t -c user.email=t@example.invalid commit -q -m "b"
side_sha="$(git -C "$fixture" rev-parse HEAD)"
git -C "$fixture" checkout -q main

ref() { "$tools/check_release_ref.sh" --root "$fixture" --main-ref main "$@"; }
expect_status "v1.2.3 on main is a release" 0 ref --tag v1.2.3 --sha "$main_sha"
expect_output "  and reports prerelease=false" '^prerelease=false$'
expect_status "v1.2.3-rc.1 on a side branch is a prerelease" 0 ref --tag v1.2.3-rc.1 --sha "$side_sha"
expect_output "  and reports prerelease=true" '^prerelease=true$'
expect_output "  and reports its suffix" '^suffix=rc\.1$'
expect_status "v1.2.3 off main is refused" 1 ref --tag v1.2.3 --sha "$side_sha"
expect_output "  and says it must be on main" 'not on main'
expect_status "a tag that disagrees with CMakeLists.txt is refused" 1 ref --tag v1.2.4 --sha "$main_sha"
expect_output "  and names both versions" 'declares 1\.2\.3'
expect_status "a tag without the v is refused" 1 ref --tag 1.2.3 --sha "$main_sha"
expect_status "a leading zero is refused" 1 ref --tag v01.2.3 --sha "$main_sha"
expect_status "a bare prerelease dash is refused" 1 ref --tag v1.2.3- --sha "$main_sha"
expect_status "a dry run takes its version from the tree" 0 ref --dry-run dryrun.9
expect_output "  and its stand-in tag is a prerelease" '^tag=v1\.2\.3-dryrun\.9$'
printf '{\n  "version": "9.9.9"\n}\n' > "$fixture/vcpkg.json"
expect_status "a vcpkg.json version that disagrees is refused" 1 ref --tag v1.2.3 --sha "$main_sha"
printf '{\n  "version": "1.2.3"\n}\n' > "$fixture/vcpkg.json"
printf '# Changelog\n\n## [Unreleased]\n' > "$fixture/CHANGELOG.md"
expect_status "a final release with no changelog section is refused" 1 ref --tag v1.2.3 --sha "$main_sha"
expect_status "a prerelease with no changelog section is allowed" 0 ref --tag v1.2.3-rc.1 --sha "$main_sha"

echo "changelog_section.sh"
printf '# C\r\n\r\n## [Unreleased]\r\n\r\nx\r\n\r\n## [1.0.0] - d\r\n\r\n### Added\r\n\r\n- a\r\n\r\n## [0.9.0]\r\n\r\nold\r\n' > "$work/changelog.md"
section="$("$tools/changelog_section.sh" 1.0.0 "$work/changelog.md")"
if [ "$section" = "$(printf '### Added\n\n- a')" ]; then ok "extracts one section, trims it, ignores CRLF"; else bad "section was: $section"; fi
expect_status "a missing section fails" 1 "$tools/changelog_section.sh" 7.7.7 "$work/changelog.md"

# --- signing and verification ---------------------------------------------------
echo "sign_release.sh and verify_release.sh"
dist="$work/dist"
mkdir -p "$dist"
printf 'one\n' > "$dist/WordJewelsSolver-1.2.3-win64.zip"
printf 'two\n' > "$dist/wordjewelssolver_1.2.3_amd64.deb"

expect_status "an ephemeral rehearsal signs and verifies" 0 "$tools/sign_release.sh" --dist "$dist" --ephemeral
[ -f "$dist/SHA256SUMS.asc" ] && [ -f "$dist/KEYS" ] && ok "  SHA256SUMS.asc and KEYS are written" || bad "  SHA256SUMS.asc or KEYS missing"
[ "$(ls "$dist"/*.asc | wc -l)" -eq 3 ] && ok "  every file and SHA256SUMS has a signature" || bad "  expected three .asc files"
expect_status "the user's check accepts it, unpinned" 0 "$tools/verify_release.sh" --dir "$dist" --unpinned
# Outside a checkout there is no committed fingerprint to fall back on (inside
# this one there is), so the script is run from a copy.
mkdir -p "$work/loose/tools/release"
cp "$tools/verify_release.sh" "$work/loose/tools/release/"
expect_status "the user's check refuses it without a pin" 1 "$work/loose/tools/release/verify_release.sh" --dir "$dist"
expect_output "  and says to pass a fingerprint" 'Pass --fingerprint'
expect_status "the user's check refuses the wrong fingerprint" 1 "$tools/verify_release.sh" --dir "$dist" --fingerprint 0000000000000000000000000000000000000000
cp -r "$dist" "$work/dist-tampered"
printf 'evil\n' > "$work/dist-tampered/WordJewelsSolver-1.2.3-win64.zip"
expect_status "a tampered file is caught" 1 "$tools/verify_release.sh" --dir "$work/dist-tampered" --unpinned
cp -r "$dist" "$work/dist-sums"
printf '%064d  injected.zip\n' 0 >> "$work/dist-sums/SHA256SUMS"
expect_status "an edited SHA256SUMS is caught by its signature" 1 "$tools/verify_release.sh" --dir "$work/dist-sums" --unpinned

# A real-key flow with a throw-away key made by the same script the author runs.
printf 'throw-away-passphrase' > "$work/pass.txt"
"$tools/new_signing_key.sh" --out "$work/key" --passphrase-file "$work/pass.txt" --expire 1d >/dev/null 2>&1
if [ -s "$work/key/private-key.asc" ] && [ -s "$work/key/public-key.asc" ] && [ -s "$work/key/fingerprint.txt" ] && [ -s "$work/key/revocation-cert.asc" ]; then
  ok "new_signing_key.sh writes the key, the public key, the fingerprint and a revocation certificate"
else
  bad "new_signing_key.sh left out a file: $(ls "$work/key" 2>&1 | tr '\n' ' ')"
fi
mkdir -p "$work/keys"
cp "$work/key/public-key.asc" "$work/keys/KEYS"; cp "$work/key/fingerprint.txt" "$work/keys/FINGERPRINT"
fingerprint="$(tr -d "[:space:]" < "$work/key/fingerprint.txt")"
private_key="$(cat "$work/key/private-key.asc")"

dist2="$work/dist2"
mkdir -p "$dist2"
printf 'three\n' > "$dist2/a.zip"
expect_status "missing secrets stop with exit 3" 3 "$tools/sign_release.sh" --dist "$dist2" --keys-dir "$work/keys"
expect_output "  and say how to fix it" 'new_signing_key'
expect_status "a wrong passphrase stops with exit 3" 3 env GPG_PRIVATE_KEY="$private_key" GPG_PASSPHRASE=wrong "$tools/sign_release.sh" --dist "$dist2" --keys-dir "$work/keys"
mkdir -p "$work/keys-other"
cp "$work/key/public-key.asc" "$work/keys-other/KEYS"
printf '0000000000000000000000000000000000000000\n' > "$work/keys-other/FINGERPRINT"
expect_status "a key that is not the committed one stops with exit 3" 3 env GPG_PRIVATE_KEY="$private_key" GPG_PASSPHRASE=throw-away-passphrase "$tools/sign_release.sh" --dist "$dist2" --keys-dir "$work/keys-other"
expect_output "  and names both fingerprints" 'fingerprint is'
expect_status "the right secrets sign" 0 env GPG_PRIVATE_KEY="$private_key" GPG_PASSPHRASE=throw-away-passphrase "$tools/sign_release.sh" --dist "$dist2" --keys-dir "$work/keys"
expect_status "and verify against the pinned fingerprint" 0 "$tools/verify_release.sh" --dir "$dist2" --fingerprint "$fingerprint"

# --- the SBOM ---------------------------------------------------------------------
echo "generate_sbom.ps1"
pkgs="$work/pkgs"
mkdir -p "$pkgs/stage/bin"
: > "$pkgs/stage/bin/Qt6Core.dll"
: > "$pkgs/stage/bin/vcruntime140.dll"
(cd "$pkgs/stage" && pwsh -NoProfile -Command 'Compress-Archive -Path bin -DestinationPath ../WordJewelsSolver-1.2.3-win64.zip') >/dev/null 2>&1
rm -rf "$pkgs/stage"
printf 'deb\n' > "$pkgs/wordjewelssolver_1.2.3_amd64.deb"
mkdir -p "$work/spdx"
cat > "$work/spdx/vcpkg.spdx.json" <<'EOF'
{"spdxVersion":"SPDX-2.2","packages":[{"name":"catch2","versionInfo":"3.0.0","licenseConcluded":"BSL-1.0"}]}
EOF
sbom="$work/out.cdx.json"
expect_status "an SBOM is generated" 0 pwsh -NoProfile -File "$tools/generate_sbom.ps1" -Tag v1.2.3-rc.1 -Commit "$main_sha" -Repository example/repo -PackagesDir "$pkgs" -VcpkgSpdxDir "$work/spdx" -Output "$sbom" -Root "$repo" -Timestamp 2026-01-01T00:00:00Z
if [ -s "$sbom" ] && jq -e '.bomFormat == "CycloneDX" and .specVersion == "1.6" and (.serialNumber | test("^urn:uuid:[0-9a-f-]{36}$"))' "$sbom" >/dev/null; then ok "  it is a CycloneDX 1.6 document with a serial number"; else bad "  not a CycloneDX 1.6 document"; fi
jq -e '.metadata.component.version == "1.2.3-rc.1" and .metadata.component.purl == "pkg:github/example/repo@v1.2.3-rc.1"' "$sbom" >/dev/null && ok "  it names the release by its tag" || bad "  wrong version or purl"
jq -e '[.components[].name] | (index("qtquick3d") != null) and (index("Microsoft Visual C++ Runtime") != null)' "$sbom" >/dev/null && ok "  Qt and the C++ runtime are there when a ZIP carries them" || bad "  Qt or the runtime missing"
jq -e '[.components[] | select(.name == "catch2")] | length == 1 and .[0].scope == "excluded"' "$sbom" >/dev/null && ok "  vcpkg's port is there, excluded from the product" || bad "  catch2 missing or in scope"
jq -e '.metadata.component.externalReferences | map(select(.type == "distribution" and (.hashes[0].content | test("^[0-9a-f]{64}$")))) | length == 2' "$sbom" >/dev/null && ok "  both release files are referenced with their SHA-256" || bad "  distribution references wrong"
jq -e '[.components[] | select(.type == "data" and .name == "ENABLE word list (enable1.txt)")][0].hashes[0].content | test("^[0-9a-f]{64}$")' "$sbom" >/dev/null && ok "  the word list carries a computed hash" || bad "  word list hash missing"
pwsh -NoProfile -File "$tools/generate_sbom.ps1" -Tag v1.2.3-rc.1 -Commit "$main_sha" -Repository example/repo -PackagesDir "$pkgs" -VcpkgSpdxDir "$work/spdx" -Output "$work/again.cdx.json" -Root "$repo" -Timestamp 2026-01-01T00:00:00Z >/dev/null 2>&1
cmp -s "$sbom" "$work/again.cdx.json" && ok "  the same inputs give the same bytes" || bad "  the output is not deterministic"
# Against the specification's own schema, when the caller has fetched it
# (release-tooling.yml does): WJS_CDX_SCHEMA_DIR names the folder, WJS_PYTHON a
# python with jsonschema and referencing.
if [ -n "${WJS_CDX_SCHEMA_DIR:-}" ]; then
  expect_status "  it validates against the CycloneDX 1.6 JSON Schema" 0 "${WJS_PYTHON:-python3}" -I "$repo/tests/package/validate_sbom.py" "$sbom" "$WJS_CDX_SCHEMA_DIR"
else
  echo "  skip  the CycloneDX schema check (set WJS_CDX_SCHEMA_DIR and WJS_PYTHON)"
fi
rm -rf "$pkgs"; mkdir -p "$pkgs"; printf 'deb\n' > "$pkgs/p.deb"
pwsh -NoProfile -File "$tools/generate_sbom.ps1" -Tag v1.2.3 -Commit "$main_sha" -PackagesDir "$pkgs" -Output "$work/cli.cdx.json" -Root "$repo" >/dev/null 2>&1
jq -e '[.components[].name] | index("qtbase") == null' "$work/cli.cdx.json" >/dev/null && ok "  a command-line-only release does not claim Qt" || bad "  Qt claimed without a Qt package"

# --- the iOS over-the-air manifest -------------------------------------------------
echo "make_ios_manifest.sh"
manifest="$work/manifest.plist"
ios_args=(--out "$manifest" --ipa WordJewelsSolver-1.2.3-rc.1-ios-adhoc.ipa --repo example/repo --tag v1.2.3-rc.1 --version 1.2.3)
expect_status "a manifest is written" 0 "$tools/make_ios_manifest.sh" "${ios_args[@]}"
grep -q '<string>https://github.com/example/repo/releases/download/v1.2.3-rc.1/WordJewelsSolver-1.2.3-rc.1-ios-adhoc.ipa</string>' "$manifest" \
  && ok "  it names the release asset's URL" || bad "  the IPA's URL is wrong"
grep -q '<string>org.iainchesworth.wordjewelssolver</string>' "$manifest" && ok "  it names the bundle identifier" || bad "  the bundle identifier is missing"
grep -q '<key>bundle-version</key>' "$manifest" && grep -q '<string>1.2.3</string>' "$manifest" && ok "  it carries the plain version" || bad "  the version is missing"
if command -v xmllint >/dev/null 2>&1; then
  xmllint --noout "$manifest" 2>/dev/null && ok "  it is well-formed XML" || bad "  not well-formed XML"
else
  echo "  skip  the well-formedness check (no xmllint)"
fi
expect_status "a file name with a space is refused" 2 "$tools/make_ios_manifest.sh" --out "$work/x.plist" --ipa "a b.ipa" --repo example/repo --tag v1.2.3 --version 1.2.3
expect_status "a tag that is not a release tag is refused" 2 "$tools/make_ios_manifest.sh" --out "$work/x.plist" --ipa a.ipa --repo example/repo --tag main --version 1.2.3
expect_status "a version with a suffix is refused (the tag carries it)" 2 "$tools/make_ios_manifest.sh" --out "$work/x.plist" --ipa a.ipa --repo example/repo --tag v1.2.3 --version 1.2.3-rc.1
expect_status "a repository that is not owner/name is refused" 2 "$tools/make_ios_manifest.sh" --out "$work/x.plist" --ipa a.ipa --repo 'a&b' --tag v1.2.3 --version 1.2.3

# --- the package check's refusal of development leftovers ---------------------------
echo "check_package.ps1"
expect_status "a Release package must not carry Tracy, an ASan runtime or a pdb" 0 pwsh -NoProfile -File "$repo/tests/package/test_check_package_forbid.ps1"

# --- every shell script is executable in the repository ---------------------------------
# A script committed from a machine without file modes (Windows) lands as 100644,
# and a workflow that runs it by path then fails with "Permission denied".
echo "executable bits"
not_executable="$(git -C "$repo" ls-files -s -- '*.sh' | awk '$1 != "100755" {print $4}')"
if [ -z "$not_executable" ]; then ok "every tracked *.sh is mode 100755"; else bad "not executable in git (git update-index --chmod=+x): $not_executable"; fi

# --- the release workflow ---------------------------------------------------------
echo "release.yml"
workflow="$repo/.github/workflows/release.yml"
versions="$repo/.github/toolchain-versions.json"
cmake_pin="$(jq -r .cmake "$versions")"
ninja_pin="$(jq -r .ninja "$versions")"
qt_version="$(jq -r .qt.version "$versions")"
qt_modules="$(jq -r .qt.modules "$versions")"
qt_action="$(jq -r .qt.action "$versions")"
aqt_source="$(jq -r .qt.aqtsource "$versions")"
grep -q "cmakeVersion: \"$cmake_pin\"" "$workflow" && ok "CMake is the pinned $cmake_pin" || bad "CMake pin differs from toolchain-versions.json"
grep -q "ninjaVersion: \"$ninja_pin\"" "$workflow" && ok "Ninja is the pinned $ninja_pin" || bad "Ninja pin differs from toolchain-versions.json"
grep -q "uses: $qt_action" "$workflow" && ok "Qt is installed by the pinned action" || bad "install-qt-action differs from toolchain-versions.json"
grep -q "version: \"$qt_version\"" "$workflow" && grep -q "modules: \"$qt_modules\"" "$workflow" && grep -q "aqtsource: \"$aqt_source\"" "$workflow" \
  && ok "Qt's version, modules and aqtsource are the pinned ones" || bad "Qt settings differ from toolchain-versions.json"
for platform in windows linux macos; do
  kit_host="$(jq -r ".qt.kits.$platform.host" "$versions")"
  kit_arch="$(jq -r ".qt.kits.$platform.arch" "$versions")"
  kit_dir="$(jq -r ".qt.kits.$platform.dir" "$versions")"
  if grep -q "qt-host: $kit_host" "$workflow" && grep -q "qt-arch: $kit_arch" "$workflow" && grep -q "qt-dir: $kit_dir" "$workflow"; then
    ok "the $platform Qt kit is $kit_arch on $kit_host, as pinned"
  else
    bad "the $platform Qt kit in release.yml differs from toolchain-versions.json ($kit_host $kit_arch $kit_dir)"
  fi
done
if grep -E '^\s*(-\s*)?uses:\s*[^./ ]' "$workflow" | grep -Ev '@[0-9a-f]{40}( |$)' | grep -q .; then
  bad "an action in release.yml is not pinned to a full commit SHA"
else
  ok "every action is pinned to a full commit SHA"
fi
grep -q 'WJS_ENABLE_TRACY' "$workflow" && bad "release.yml mentions WJS_ENABLE_TRACY; the presets decide" || ok "release.yml leaves Tracy to the presets"
grep -Eq '^permissions: \{\}$' "$workflow" && ok "no permissions at the top level" || bad "release.yml has top-level permissions"
if awk '/^\s*run:/{r=1} r && /\$\{\{ *(github\.event|inputs|github\.head_ref)/ {print; bad=1} END{exit bad}' "$workflow" >/dev/null; then ok "no event or input data is interpolated into a run block"; else bad "event or input data in a run block"; fi
grep -q 'persist-credentials: false' "$workflow" && ok "checkouts do not persist credentials" || bad "a checkout persists credentials"
ios_secrets='IOS_DIST_CERTIFICATE_BASE64|IOS_DIST_CERTIFICATE_PASSWORD|IOS_PROVISIONING_PROFILE_BASE64|APPLE_TEAM_ID|APPLE_PROVISIONING_PROFILE_NAME'
android_secrets='ANDROID_KEYSTORE_BASE64|ANDROID_KEYSTORE_PASSWORD|ANDROID_KEY_ALIAS|ANDROID_KEY_PASSWORD'
if grep -E 'secrets\.' "$workflow" | grep -Ev "GPG_PRIVATE_KEY|GPG_PASSPHRASE|MACOS_CERTIFICATE|MACOS_SIGN_IDENTITY|NOTARY_APPLE_ID|NOTARY_TEAM_ID|NOTARY_PASSWORD|$ios_secrets|$android_secrets" | grep -q .; then bad "release.yml uses a secret other than the GPG, macOS, iOS and Android signing secrets"; else ok "the only secrets are the GPG signing secrets, the macOS ones, the five iOS ones and the four Android ones"; fi

# --- the iOS job: its secrets, its kit and what it hands to the signing job ---------
echo "release.yml, iOS"
for secret in IOS_DIST_CERTIFICATE_BASE64 IOS_DIST_CERTIFICATE_PASSWORD IOS_PROVISIONING_PROFILE_BASE64 APPLE_TEAM_ID APPLE_PROVISIONING_PROFILE_NAME; do
  if [ "$(grep -c "secrets\.$secret" "$workflow")" -ge 2 ] && grep -q "\[ -n \"\$$secret\" \] || missing=" "$workflow"; then
    ok "$secret is required by name before anything is built, and used"
  else
    bad "$secret is not both checked for by the early step and used by a later one in release.yml"
  fi
done
require_line="$(grep -n 'name: Require the Apple signing secrets' "$workflow" | cut -d: -f1)"
qt_line="$(grep -n 'name: Install Qt (iOS kit and macOS host kit)' "$workflow" | cut -d: -f1)"
if [ -n "$require_line" ] && [ -n "$qt_line" ] && [ "$require_line" -lt "$qt_line" ]; then ok "the missing-secret step comes before Qt is installed"; else bad "no 'Require the Apple signing secrets' step before the Qt install"; fi
ios_kit_host="$(jq -r .qt.kits.ios.host "$versions")"
ios_kit_arch="$(jq -r .qt.kits.ios.arch "$versions")"
ios_kit_dir="$(jq -r .qt.kits.ios.dir "$versions")"
ios_host_dir="$(jq -r .qt.kits.ios.hostdir "$versions")"
for file in "$workflow" "$repo/.github/workflows/ios-build.yml"; do
  name="$(basename "$file")"
  if grep -q "host: $ios_kit_host" "$file" && grep -q 'target: ios' "$file" && grep -q "arch: $ios_kit_arch" "$file" \
    && grep -q "Qt/$qt_version/$ios_kit_dir" "$file" && grep -q "Qt/$qt_version/$ios_host_dir" "$file"; then
    ok "$name installs the pinned iOS kit ($ios_kit_arch on $ios_kit_host, with the $ios_host_dir host kit)"
  else
    bad "$name's iOS Qt kit differs from toolchain-versions.json (qt.kits.ios)"
  fi
  grep -q "cmakeVersion: \"$cmake_pin\"" "$file" && grep -q "ninjaVersion: \"$ninja_pin\"" "$file" && grep -q "uses: $qt_action" "$file" \
    && grep -q "version: \"$qt_version\"" "$file" && grep -q "modules: \"$qt_modules\"" "$file" && grep -q "aqtsource: \"$aqt_source\"" "$file" \
    && ok "$name pins CMake, Ninja and Qt as toolchain-versions.json does" || bad "$name's toolchain pins differ from toolchain-versions.json"
  if grep -E '^\s*(-\s*)?uses:\s*[^./ ]' "$file" | grep -Ev '@[0-9a-f]{40}( |$)' | grep -q .; then bad "an action in $name is not pinned to a full commit SHA"; else ok "$name pins every action to a full commit SHA"; fi
  grep -Eq '^permissions: \{\}$' "$file" && ok "$name has no permissions at the top level" || bad "$name has top-level permissions"
done
grep -q 'name: package-ios' "$workflow" && grep -q 'pattern: package-\*' "$workflow" && ok "the IPA and its manifest are uploaded as package-ios, which the signing job downloads with the other packages" || bad "package-ios is not uploaded, or the signing job does not take package-*"
if grep -E 'secrets\.' "$repo/.github/workflows/ios-build.yml" | grep -q .; then bad "ios-build.yml (the unsigned check) uses a secret"; else ok "ios-build.yml uses no secret"; fi
# One bundle identifier everywhere (it is the App ID the profile was made for).
bundle_id="org.iainchesworth.wordjewelssolver"
for file in "$workflow" "$repo/libs/platforms/ios/platform.cmake" "$repo/libs/platforms/macos/platform.cmake" "$repo/libs/platforms/ios/packaging/verify_ipa.sh"; do
  grep -q "$bundle_id" "$file" && ok "$(basename "$file") says $bundle_id" || bad "$(basename "$file") does not say $bundle_id"
done
grep -q 'NSCameraUsageDescription' "$repo/libs/platforms/ios/packaging/Info.plist.in" \
  && [ "$(grep -A1 NSCameraUsageDescription "$repo/libs/platforms/ios/packaging/Info.plist.in" | sed -n 2p | sed 's/^ *//')" = "$(grep -A1 NSCameraUsageDescription "$repo/libs/platforms/macos/packaging/Info.plist.in" | sed -n 2p | sed 's/^ *//')" ] \
  && ok "the iOS camera usage text is the macOS one" || bad "the iOS camera usage text differs from the macOS one"

# --- the Android build: the unsigned check's pins and the release job's secrets ----------
# (tests/platform/check_android.cmake holds the rest: the kit, the NDK, the secrets by name.)
echo "android-build.yml"
file="$repo/.github/workflows/android-build.yml"
grep -q "cmakeVersion: \"$cmake_pin\"" "$file" && grep -q "ninjaVersion: \"$ninja_pin\"" "$file" && grep -q "uses: $qt_action" "$file"   && ok "android-build.yml pins CMake, Ninja and the Qt action as toolchain-versions.json does" || bad "android-build.yml's toolchain pins differ from toolchain-versions.json"
if grep -E '^\s*(-\s*)?uses:\s*[^./ ]' "$file" | grep -Ev '@[0-9a-f]{40}( |$)' | grep -q .; then bad "an action in android-build.yml is not pinned to a full commit SHA"; else ok "android-build.yml pins every action to a full commit SHA"; fi
grep -Eq '^permissions: \{\}$' "$file" && ok "android-build.yml has no permissions at the top level" || bad "android-build.yml has top-level permissions"
if grep -E 'secrets\.' "$file" | grep -q .; then bad "android-build.yml (the debug-signed build) uses a secret"; else ok "android-build.yml uses no secret"; fi
grep -q 'name: package-android' "$workflow" && ok "the Android packages are uploaded as package-android, which the signing job downloads with the other packages" || bad "package-android is not uploaded by release.yml"

echo
if [ "$failures" -eq 0 ]; then
  echo "release tooling: $checks check(s) passed"
else
  echo "release tooling: $failures of $checks check(s) FAILED" >&2
  exit 1
fi
