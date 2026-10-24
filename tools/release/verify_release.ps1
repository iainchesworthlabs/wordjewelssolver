#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Verify a WordJewelsSolver release: the signing key, the signatures, the
    checksums and (optionally) the build-provenance attestations.

.DESCRIPTION
    The same checks, in the same order, as verify_release.sh, for a Windows
    shell. It stops at the first failure.

      1. the key in KEYS has the fingerprint you expect (-Fingerprint, else
         .github/release-signing/FINGERPRINT in a checkout of this repository;
         it should match SECURITY.md and the release page). -Unpinned skips this
         and says so, for a dry run's ephemeral key;
      2. SHA256SUMS.asc is a good signature by that key over SHA256SUMS, and so
         is every other <file>.asc over its file;
      3. every file listed in SHA256SUMS has its recorded SHA-256;
      4. with -Attest: gh attestation verify for every listed file.

    GnuPG runs in a private temporary home; your keyring is not touched. It
    needs gpg (Gpg4win, or Git for Windows' usr\bin\gpg.exe) and, for -Repo/-Tag
    and -Attest, the GitHub CLI.

.PARAMETER Dir
    The folder holding the downloaded files, SHA256SUMS and KEYS among them.

.PARAMETER Repo
    owner/name. With -Tag and no -Dir, the release's files are downloaded.

.PARAMETER Tag
    The release tag, e.g. v0.1.0.

.PARAMETER Fingerprint
    The 40-hex fingerprint to expect.

.PARAMETER Keys
    A public key file, if not the KEYS beside the release.

.PARAMETER Attest
    Also verify the attestations (needs -Repo).

.PARAMETER Unpinned
    Do not compare the key's fingerprint with anything.

.EXAMPLE
    ./tools/release/verify_release.ps1 -Dir .\downloads -Fingerprint <fingerprint from SECURITY.md>
#>
[CmdletBinding()]
param(
    [string]$Dir,
    [string]$Repo,
    [string]$Tag,
    [string]$Fingerprint,
    [string]$Keys,
    [switch]$Attest,
    [switch]$Unpinned
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$text) {
    Write-Host "FAILED: $text" -ForegroundColor Red
    exit 1
}

function Find-Gpg {
    $command = Get-Command gpg -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
            (Join-Path $env:ProgramFiles 'Git\usr\bin\gpg.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'GnuPG\bin\gpg.exe'))) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    return $null
}

$gpg = Find-Gpg
if (-not $gpg) { Fail 'gpg was not found (install Gpg4win, or Git for Windows, which ships one in usr\bin).' }

# Git for Windows' gpg is an MSYS program: its agent refuses a socket directory
# written as C:\..., so the home directory is handed over as /c/... for it.
function ConvertTo-GnupgHome([string]$Path) {
    if ($gpg -match '[\\/]usr[\\/]bin[\\/]') {
        $unix = $Path -replace '^([A-Za-z]):', '/$1'
        return $unix.Replace('\', '/')
    }
    return $Path
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("wjs-verify-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $temp | Out-Null
$gnupgHome = Join-Path $temp 'gnupg'
New-Item -ItemType Directory -Path $gnupgHome | Out-Null

try {
    if (-not $Dir) {
        if (-not ($Repo -and $Tag)) { throw 'Give -Dir, or -Repo and -Tag.' }
        if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Fail '-Repo/-Tag need the GitHub CLI (gh).' }
        $Dir = Join-Path $temp 'release'
        New-Item -ItemType Directory -Path $Dir | Out-Null
        Write-Host "==> downloading $Repo $Tag into $Dir"
        & gh release download $Tag --repo $Repo --dir $Dir
        if ($LASTEXITCODE -ne 0) { Fail 'gh release download failed.' }
    }
    $Dir = (Resolve-Path -LiteralPath $Dir).Path
    foreach ($required in 'SHA256SUMS', 'SHA256SUMS.asc') {
        if (-not (Test-Path -LiteralPath (Join-Path $Dir $required))) { Fail "$Dir\$required is missing." }
    }
    if (-not $Keys) { $Keys = Join-Path $Dir 'KEYS' }
    if (-not (Test-Path -LiteralPath $Keys)) { Fail "no public key: $Keys does not exist (pass -Keys)." }

    function Invoke-Gpg([string[]]$Arguments) {
        $output = & $gpg --quiet --no-auto-check-trustdb --homedir (ConvertTo-GnupgHome $gnupgHome) --batch @Arguments 2>&1
        return [pscustomobject]@{ Exit = $LASTEXITCODE; Output = @($output | ForEach-Object { "$_" }) }
    }

    # 1. The key.
    $import = Invoke-Gpg @('--import', $Keys)
    if ($import.Exit -ne 0) { Fail "$Keys is not a public key block gpg can import." }
    $listing = Invoke-Gpg @('--with-colons', '--list-keys')
    $first = $listing.Output | Where-Object { $_ -like 'fpr:*' } | Select-Object -First 1
    if (-not $first) { Fail "no key was imported from $Keys." }
    $actual = ($first -split ':')[9]

    if (-not $Fingerprint) {
        $pinned = Join-Path $PSScriptRoot '../../.github/release-signing/FINGERPRINT'
        if (Test-Path -LiteralPath $pinned) {
            $Fingerprint = (Get-Content -LiteralPath $pinned -Raw)
            Write-Host "==> expected fingerprint read from the checkout's .github/release-signing/FINGERPRINT"
        }
    }
    $Fingerprint = if ($Fingerprint) { ($Fingerprint -replace '\s', '').ToUpperInvariant() } else { '' }
    if ($Unpinned) {
        Write-Warning "-Unpinned: the key's fingerprint ($actual) was not compared with anything. This proves the files are consistent with $Keys, nothing more."
    }
    elseif (-not $Fingerprint) {
        Fail 'no expected fingerprint. Pass -Fingerprint <40 hex> (the one in SECURITY.md and on the release page), or -Unpinned to skip this step knowingly.'
    }
    elseif ($actual -ne $Fingerprint) {
        Fail "the key in $Keys has fingerprint $actual, not the expected $Fingerprint. Do not trust these files."
    }
    else {
        Write-Host "==> key $actual matches the expected fingerprint"
    }

    # 2. The signatures.
    $signatures = @(Get-ChildItem -LiteralPath $Dir -Filter '*.asc' -File)
    if ($signatures.Count -eq 0) { Fail "no .asc signatures in $Dir." }
    foreach ($signature in $signatures) {
        $file = $signature.FullName.Substring(0, $signature.FullName.Length - 4)
        if (-not (Test-Path -LiteralPath $file)) { Fail "$($signature.Name) has no file $([IO.Path]::GetFileName($file)) beside it." }
        $result = Invoke-Gpg @('--status-fd', '1', '--verify', $signature.FullName, $file)
        if (-not ($result.Output | Where-Object { $_ -like "``[GNUPG:``] VALIDSIG $actual *" })) {
            Fail "$($signature.Name) is not a valid signature by $actual over $([IO.Path]::GetFileName($file))."
        }
    }
    Write-Host "==> $($signatures.Count) signature(s) verified"

    # 3. The checksums.
    $listed = @()
    foreach ($line in Get-Content -LiteralPath (Join-Path $Dir 'SHA256SUMS')) {
        if (-not $line.Trim()) { continue }
        if ($line -notmatch '^([0-9a-fA-F]{64}) [ *](.+)$') { Fail "SHA256SUMS has a line this script cannot read: $line" }
        $expectedHash, $name = $Matches[1].ToLowerInvariant(), $Matches[2]
        $path = Join-Path $Dir $name
        if (-not (Test-Path -LiteralPath $path)) { Fail "SHA256SUMS lists $name but it is not in $Dir." }
        $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expectedHash) { Fail "$name does not match SHA256SUMS (got $actualHash)." }
        Write-Host "    ${name}: OK"
        $listed += $name
    }

    # 4. The attestations.
    if ($Attest) {
        if (-not $Repo) { Fail '-Attest needs -Repo <owner/name>.' }
        if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Fail '-Attest needs the GitHub CLI (gh).' }
        foreach ($name in $listed) {
            & gh attestation verify (Join-Path $Dir $name) --repo $Repo `
                --signer-workflow "$Repo/.github/workflows/release.yml" *> $null
            if ($LASTEXITCODE -ne 0) { Fail "no valid build-provenance attestation from $Repo's release workflow for $name." }
            Write-Host "    attested: ${name}"
        }
        Write-Host '==> attestations verified'
    }

    Write-Host "OK: $(Split-Path -Leaf $Dir) verified." -ForegroundColor Green
}
finally {
    $agentTool = Join-Path (Split-Path $gpg) 'gpgconf.exe'
    if (Test-Path -LiteralPath $agentTool) { & $agentTool --homedir (ConvertTo-GnupgHome $gnupgHome) --kill all 2>$null | Out-Null }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
