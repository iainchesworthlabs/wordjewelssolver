#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Create the release signing key pair, ON YOUR OWN MACHINE, in a folder of
    your choice outside the repository.

.DESCRIPTION
    Run by the repository's author in their own terminal. Nothing secret is
    printed: the secrets are written to files, and the files are what you give
    to `gh secret set`. The script

      1. makes a temporary GnuPG home directory (your own keyring is never read
         or changed) and a second and a third for the check below;
      2. generates an Ed25519, sign-only primary key, expiring in -Expire
         (default 2 years), user id "WordJewelsSolver Release <address>";
      3. generates a random 32-byte passphrase and protects the secret key with it;
      4. writes, into -OutDir (default ~/wjs-release-key, ACL restricted to you;
         a folder inside a git working tree is refused):
           private-key.asc      the armoured secret key, passphrase-protected
           public-key.asc       the armoured public key
           revocation-cert.asc  a revocation certificate (keep it OFFLINE)
           passphrase.txt       the passphrase
           fingerprint.txt      the 40-hex fingerprint
           README.md            the matching `gh secret set` commands
      5. proves the exported files work: imports the private key into a fresh
         home, signs a test file with it, imports ONLY the public key into
         another fresh home and verifies the signature there;
      6. overwrites and deletes the temporary homes.

    It does not upload anything, contact GitHub or touch the repository, except
    that -Install copies the PUBLIC key and fingerprint to
    .github/release-signing/KEYS and FINGERPRINT, where release.yml expects them.

    It needs gpg (GnuPG 2.2 or later) on PATH, or Git for Windows' usr\bin\gpg.exe
    or Gpg4win. Git for Windows' gpg is an MSYS program whose agent refuses a home
    directory written C:\..., so the script hands it /c/... instead.

.PARAMETER OutDir
    Where the files go. Default: ~/wjs-release-key. Must be empty or not exist,
    and must not be inside any git working tree.

.PARAMETER Email
    The address in the user id. Default: git config user.email.

.PARAMETER Name
    The name in the user id. Default "WordJewelsSolver Release".

.PARAMETER Expire
    gpg expiry: 2y (default), 18m, 0 for none.

.PARAMETER Repo
    owner/name the README's gh commands name.

.PARAMETER Install
    Also copy the public key and fingerprint to .github/release-signing/.

.EXAMPLE
    ./tools/release/new_signing_key.ps1
    ./tools/release/new_signing_key.ps1 -OutDir D:\keys\wjs -Email you@example.org -Install
#>
[CmdletBinding()]
param(
    [string]$OutDir = (Join-Path $HOME 'wjs-release-key'),
    [string]$Email = '',
    [string]$Name = 'WordJewelsSolver Release',
    [string]$Expire = '2y',
    [string]$Repo = 'iainchesworthlabs/wordjewelssolver',
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$text) {
    Write-Host "FAILED: $text" -ForegroundColor Red
    exit 1
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path

# --- gpg ---------------------------------------------------------------------

function Find-Gpg {
    $command = Get-Command gpg -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @(
            (Join-Path $env:ProgramFiles 'Git\usr\bin\gpg.exe'),
            (Join-Path ${env:ProgramFiles(x86)} 'GnuPG\bin\gpg.exe'),
            (Join-Path $env:ProgramFiles 'GnuPG\bin\gpg.exe'))) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    return $null
}

$gpg = Find-Gpg
if (-not $gpg) { Fail 'gpg was not found. Install Git for Windows (it ships gpg in usr\bin) or Gpg4win, and run again.' }
$gpgVersion = (& $gpg --version | Select-Object -First 1)
Write-Host "==> using $gpg ($gpgVersion)"
$isMsys = $gpg -match '[\\/]usr[\\/]bin[\\/]'

# The MSYS build wants its home directory as /c/Users/..., not C:\Users\...
function ConvertTo-GnupgHome([string]$Path) {
    if ($isMsys -and $Path -match '^[A-Za-z]:') {
        return ($Path -replace '^([A-Za-z]):', '/$1').Replace('\', '/')
    }
    return $Path
}

# --- the output folder ---------------------------------------------------------

if (-not $Email) {
    $Email = (& git config user.email 2>$null)
    if (-not $Email) { Fail 'no address for the key: pass -Email, or set git config user.email.' }
}
$userId = "$Name <$Email>"

$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$comparison = if ($IsWindows) { [System.StringComparison]::OrdinalIgnoreCase } else { [System.StringComparison]::Ordinal }
$repoPrefix = $repoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (($OutDir + [System.IO.Path]::DirectorySeparatorChar).StartsWith($repoPrefix, $comparison)) {
    Fail "$OutDir is inside the repository working tree ($repoRoot). Secrets must never be where a commit can pick them up; choose a folder elsewhere (the default is ~/wjs-release-key)."
}
# Any other git working tree is as bad: look at the nearest folder that exists.
$probe = $OutDir
while ($probe -and -not (Test-Path -LiteralPath $probe)) { $probe = Split-Path -Parent $probe }
if ($probe) {
    $inside = (& git -C $probe rev-parse --is-inside-work-tree 2>$null)
    if ($inside -eq 'true') { Fail "$OutDir is inside a git working tree. Choose a folder that is not under version control." }
}
if ((Test-Path -LiteralPath $OutDir) -and (Get-ChildItem -LiteralPath $OutDir -Force | Select-Object -First 1)) {
    Fail "$OutDir already exists and is not empty. Refusing to overwrite a key; move it away or choose another -OutDir."
}

function Protect-Folder([string]$Path) {
    if ($IsWindows) {
        # Only the current user, inherited permissions removed.
        $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        & icacls.exe $Path /inheritance:r /grant:r "*${sid}:(OI)(CI)F" | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "icacls could not restrict $Path to your account." }
    }
    else {
        & chmod 700 $Path
    }
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Protect-Folder $OutDir

# --- temporary keyrings ---------------------------------------------------------

$tempHomes = [System.Collections.Generic.List[string]]::new()
function New-TempHome {
    $path = Join-Path ([System.IO.Path]::GetTempPath()) ('wjs-gpg-' + [guid]::NewGuid().ToString('N').Substring(0, 10))
    New-Item -ItemType Directory -Path $path | Out-Null
    Protect-Folder $path
    $tempHomes.Add($path)
    return $path
}

# -Interactive is for the one command that cannot run with --batch (--gen-revoke);
# its questions are answered on standard input (--command-fd), with no terminal.
function Invoke-Gpg([string]$GnupgHome, [string[]]$Arguments, [string]$Stdin = $null, [switch]$Interactive) {
    $mode = if ($Interactive) { @('--no-tty') } else { @('--batch') }
    $all = @('--quiet', '--no-auto-check-trustdb', '--homedir', (ConvertTo-GnupgHome $GnupgHome), '--yes') + $mode + $Arguments
    if ($null -ne $Stdin) { $output = $Stdin | & $gpg @all 2>&1 } else { $output = & $gpg @all 2>&1 }
    return [pscustomobject]@{ Exit = $LASTEXITCODE; Output = @($output | ForEach-Object { "$_" }) }
}

# Overwrite, then delete: a temporary home held the secret key and the passphrase's
# use. (On a journalling or SSD-backed disk an overwrite is a courtesy, not a
# guarantee; the home was in the user-only temp folder and held nothing the
# exported files do not.)
function Remove-Securely([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    Get-ChildItem -LiteralPath $Path -Recurse -Force -File -ErrorAction SilentlyContinue | ForEach-Object {
        try {
            $length = $_.Length
            if ($length -gt 0) {
                $junk = [byte[]]::new([int][Math]::Min($length, 16MB))
                [System.Security.Cryptography.RandomNumberGenerator]::Fill($junk)
                $stream = [System.IO.File]::Open($_.FullName, 'Open', 'Write')
                try {
                    $written = 0L
                    while ($written -lt $length) {
                        $chunk = [int][Math]::Min($junk.Length, $length - $written)
                        $stream.Write($junk, 0, $chunk)
                        $written += $chunk
                    }
                    $stream.Flush($true)
                }
                finally { $stream.Dispose() }
            }
        }
        catch { }
    }
    Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
}

function Stop-Agent([string]$GnupgHome) {
    $tool = Join-Path (Split-Path -Parent $gpg) $(if ($IsWindows) { 'gpgconf.exe' } else { 'gpgconf' })
    if (Test-Path -LiteralPath $tool) { & $tool --homedir (ConvertTo-GnupgHome $GnupgHome) --kill all 2>$null | Out-Null }
}

$succeeded = $false
try {
    $passphrasePath = Join-Path $OutDir 'passphrase.txt'
    $bytes = [byte[]]::new(32)
    [System.Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
    # No trailing newline: `gh secret set` would store it, and gpg would not mind.
    [System.IO.File]::WriteAllText($passphrasePath, [Convert]::ToBase64String($bytes))

    # --- generate ------------------------------------------------------------------
    $generate = New-TempHome
    Write-Host "==> generating an Ed25519 sign-only key for '$userId' (expires: $Expire)" -ForegroundColor Cyan
    $result = Invoke-Gpg $generate @('--pinentry-mode', 'loopback', '--passphrase-file', $passphrasePath,
        '--quick-generate-key', $userId, 'ed25519', 'sign', $Expire)
    if ($result.Exit -ne 0) { Fail "gpg could not generate the key: $($result.Output -join ' ')" }

    $listing = Invoke-Gpg $generate @('--with-colons', '--list-secret-keys')
    $fingerprintLine = $listing.Output | Where-Object { $_ -like 'fpr:*' } | Select-Object -First 1
    if (-not $fingerprintLine) { Fail "could not read the new key's fingerprint." }
    $fingerprint = ($fingerprintLine -split ':')[9]
    if ($fingerprint -notmatch '^[0-9A-F]{40}$') { Fail "unexpected fingerprint '$fingerprint'." }

    $publicPath = Join-Path $OutDir 'public-key.asc'
    $privatePath = Join-Path $OutDir 'private-key.asc'
    $revocationPath = Join-Path $OutDir 'revocation-cert.asc'

    $result = Invoke-Gpg $generate @('--armor', '--output', $publicPath, '--export', $fingerprint)
    if ($result.Exit -ne 0) { Fail "could not export the public key: $($result.Output -join ' ')" }
    $result = Invoke-Gpg $generate @('--pinentry-mode', 'loopback', '--passphrase-file', $passphrasePath,
        '--armor', '--output', $privatePath, '--export-secret-keys', $fingerprint)
    if ($result.Exit -ne 0) { Fail "could not export the secret key: $($result.Output -join ' ')" }

    # gpg --gen-revoke asks four questions; --command-fd answers them: yes, reason
    # 0 (no reason), no description, yes.
    $result = Invoke-Gpg $generate @('--pinentry-mode', 'loopback', '--passphrase-file', $passphrasePath,
        '--command-fd', '0', '--armor', '--output', $revocationPath, '--gen-revoke', $fingerprint) "y`n0`n`ny`n" -Interactive
    if ($result.Exit -ne 0 -or -not (Test-Path -LiteralPath $revocationPath)) {
        # gpg also writes one beside the key when it makes it.
        $automatic = Join-Path $generate "openpgp-revocs.d/$fingerprint.rev"
        if (Test-Path -LiteralPath $automatic) {
            # That file has a colon in front of the armour lines, so that it cannot be
            # imported by accident; this is the one place it is meant to be.
            $lines = Get-Content -LiteralPath $automatic | ForEach-Object { $_ -replace '^:(-----)', '$1' }
            Set-Content -LiteralPath $revocationPath -Value $lines
            Write-Warning 'gpg --gen-revoke did not run unattended; the revocation certificate is the one gpg wrote when it made the key.'
        }
        else { Fail "no revocation certificate could be made: $($result.Output -join ' ')" }
    }
    [System.IO.File]::WriteAllText((Join-Path $OutDir 'fingerprint.txt'), "$fingerprint`n")

    # --- prove the exported files ----------------------------------------------------
    Write-Host '==> checking the exported files in fresh keyrings' -ForegroundColor Cyan
    $signing = New-TempHome
    $verifying = New-TempHome
    $testFile = Join-Path $signing 'test.txt'
    Set-Content -LiteralPath $testFile -Value 'WordJewelsSolver release key check'

    $result = Invoke-Gpg $signing @('--pinentry-mode', 'loopback', '--passphrase-file', $passphrasePath, '--import', $privatePath)
    if ($result.Exit -ne 0) { Fail "private-key.asc did not import with passphrase.txt: $($result.Output -join ' ')" }
    $result = Invoke-Gpg $signing @('--pinentry-mode', 'loopback', '--passphrase-file', $passphrasePath,
        '--armor', '--detach-sign', '--local-user', $fingerprint, '--output', "$testFile.asc", $testFile)
    if ($result.Exit -ne 0) { Fail "the imported private key could not sign: $($result.Output -join ' ')" }

    $result = Invoke-Gpg $verifying @('--import', $publicPath)
    if ($result.Exit -ne 0) { Fail "public-key.asc did not import: $($result.Output -join ' ')" }
    $result = Invoke-Gpg $verifying @('--status-fd', '1', '--verify', "$testFile.asc", $testFile)
    if (-not ($result.Output | Where-Object { $_ -like "``[GNUPG:``] VALIDSIG $fingerprint *" })) {
        Fail "the signature did not verify against public-key.asc: $($result.Output -join ' ')"
    }
    $result = Invoke-Gpg $verifying @('--list-secret-keys', '--with-colons')
    if ($result.Output | Where-Object { $_ -like 'sec:*' }) { Fail 'the verifying keyring holds a secret key; it must hold only the public one.' }
    # The revocation certificate is real: imported, it marks the public key revoked.
    $result = Invoke-Gpg $verifying @('--import', $revocationPath)
    if ($result.Exit -ne 0) { Fail "revocation-cert.asc did not import: $($result.Output -join ' ')" }
    $result = Invoke-Gpg $verifying @('--list-keys', '--with-colons')
    if (-not ($result.Output | Where-Object { $_ -like 'pub:r:*' })) { Fail 'revocation-cert.asc imported but did not revoke the key.' }

    $README = @"
# WordJewelsSolver release signing key

Made by ``tools/release/new_signing_key.ps1``. Fingerprint:

    $fingerprint

User id: $userId   Expires: $Expire after creation.

| File | What it is | Where it goes |
| --- | --- | --- |
| ``private-key.asc`` | the secret key, ASCII-armoured, protected by the passphrase | repository secret ``GPG_PRIVATE_KEY``, then your key store; delete it from this folder |
| ``passphrase.txt`` | the passphrase (random, 32 bytes, base64) | repository secret ``GPG_PASSPHRASE``, then your key store; delete it from this folder |
| ``public-key.asc`` | the public key | commit as ``.github/release-signing/KEYS`` (``-Install`` does) |
| ``fingerprint.txt`` | the 40-hex fingerprint | commit as ``.github/release-signing/FINGERPRINT``; publish it in SECURITY.md and the README |
| ``revocation-cert.asc`` | revokes the key if the secret is ever lost or leaked | keep it OFFLINE, not with the key; do not upload it |

## Upload the secrets

``gh secret set`` reads the value from standard input; it has no file option.
These commands send each file's contents and print nothing of them. Run them in
this folder, as the repository's admin.

PowerShell:

``````powershell
Get-Content -Raw -LiteralPath private-key.asc | gh secret set GPG_PRIVATE_KEY --repo $Repo
Get-Content -Raw -LiteralPath passphrase.txt  | gh secret set GPG_PASSPHRASE  --repo $Repo
``````

bash / Git Bash:

``````sh
gh secret set GPG_PRIVATE_KEY --repo $Repo < private-key.asc
gh secret set GPG_PASSPHRASE  --repo $Repo < passphrase.txt
``````

Check they are there (names only, never values): ``gh secret list --repo $Repo``.

## Then

1. Put ``private-key.asc`` and ``passphrase.txt`` in your own key store, and
   ``revocation-cert.asc`` somewhere offline. Then delete ``private-key.asc`` and
   ``passphrase.txt`` from this folder.
2. Commit ``.github/release-signing/KEYS`` and ``FINGERPRINT`` (copies of
   ``public-key.asc`` and ``fingerprint.txt``) in the repository.
3. Rehearse with the real key before tagging:
   ``gh workflow run release.yml --repo $Repo -f signing=repository``.

To revoke: import ``revocation-cert.asc`` (``gpg --import revocation-cert.asc``)
into a keyring that has ``public-key.asc``, then publish the revoked public key.
"@
    [System.IO.File]::WriteAllText((Join-Path $OutDir 'README.md'), $README)

    if ($Install) {
        $destination = Join-Path $repoRoot '.github/release-signing'
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath $publicPath -Destination (Join-Path $destination 'KEYS') -Force
        Copy-Item -LiteralPath (Join-Path $OutDir 'fingerprint.txt') -Destination (Join-Path $destination 'FINGERPRINT') -Force
        Write-Host "==> copied the public key and fingerprint to $destination (review with git status, then commit)" -ForegroundColor Cyan
    }
    $succeeded = $true
}
finally {
    foreach ($path in $tempHomes) { Stop-Agent $path }
    foreach ($path in $tempHomes) { Remove-Securely $path }
    if (-not $succeeded) {
        # A failed run leaves no half-made key behind.
        Remove-Securely $OutDir
    }
}

$pretty = ($fingerprint -replace '(.{4})', '$1 ').Trim()
Write-Host ''
Write-Host 'Done. The signing key is made, proved (signed and verified in fresh keyrings) and the temporary keyrings are shredded.' -ForegroundColor Green
Write-Host ''
Write-Host "  Fingerprint: $pretty"
Write-Host "  Files:       $OutDir"
foreach ($file in Get-ChildItem -LiteralPath $OutDir -File | Sort-Object Name) { Write-Host ('    {0,-20} {1,5} bytes' -f $file.Name, $file.Length) }
Write-Host ''
Write-Host ("  Next: open {0}\README.md for the two gh secret set commands. Nothing has been uploaded." -f $OutDir)
