#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Test that tools/package/check_package.ps1 refuses a Release package that
    carries a development leftover, and leaves a development package alone.

.DESCRIPTION
    Builds throw-away "build trees" (a CMakeCache.txt naming the build type, an
    empty CPackConfig.cmake) and ZIPs, runs the real script with -Archive over
    them, and reads its verdict. Nothing is compiled. Exit 0 = every case behaved.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script = Join-Path $PSScriptRoot '../../tools/package/check_package.ps1'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("wjs-forbid-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $work | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$failures = 0

function New-Tree([string]$Name, [string]$BuildType) {
    $tree = Join-Path $work $Name
    New-Item -ItemType Directory -Path $tree | Out-Null
    Set-Content -LiteralPath (Join-Path $tree 'CMakeCache.txt') -Value "CMAKE_BUILD_TYPE:STRING=$BuildType"
    Set-Content -LiteralPath (Join-Path $tree 'CPackConfig.cmake') -Value '# empty'
    return $tree
}

function New-Zip([string]$Path, [string[]]$Entries) {
    $stage = Join-Path $work ('stage-' + [guid]::NewGuid().ToString('N').Substring(0, 6))
    foreach ($entry in $Entries) {
        $target = Join-Path $stage $entry
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
        Set-Content -LiteralPath $target -Value 'x'
    }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $Path)
}

function Test-Case([string]$Description, [string]$Tree, [string]$Zip, [string[]]$ExtraArguments, [bool]$ExpectLeftover) {
    $output = & pwsh -NoProfile -File $script -Build $Tree -Archive $Zip -NoSmoke @ExtraArguments 2>&1 | Out-String
    $said = $output -match 'development leftover'
    if ($said -eq $ExpectLeftover) {
        Write-Host "  ok    $Description"
    }
    else {
        Write-Host "  FAIL  $Description (leftover reported: $said)`n$output" -ForegroundColor Red
        $script:failures++
    }
}

try {
    $release = New-Tree 'release' 'Release'
    $debug = New-Tree 'debug' 'Debug'

    $dirty = Join-Path $work 'dirty.zip'
    New-Zip $dirty @('p-0.1.0/bin/wjs-cli.exe', 'p-0.1.0/bin/TracyClient.dll')
    $asan = Join-Path $work 'asan.zip'
    New-Zip $asan @('p-0.1.0/bin/wjs-cli.exe', 'p-0.1.0/bin/clang_rt.asan_dynamic-x86_64.dll')
    $symbols = Join-Path $work 'symbols.zip'
    New-Zip $symbols @('p-0.1.0/bin/wjs-cli.exe', 'p-0.1.0/bin/WordJewelsSolver.pdb')
    $clean = Join-Path $work 'clean.zip'
    New-Zip $clean @('p-0.1.0/bin/wjs-cli.exe', 'p-0.1.0/bin/Qt6Core.dll')

    Test-Case 'a Release package with TracyClient.dll is refused' $release $dirty @() $true
    Test-Case 'a Release package with an AddressSanitizer runtime is refused' $release $asan @() $true
    Test-Case 'a Release package with a .pdb is refused' $release $symbols @() $true
    Test-Case 'a clean Release package is not refused for leftovers' $release $clean @() $false
    Test-Case 'a Debug (development) package may carry the profiler' $debug $dirty @() $false
    Test-Case '-ForbidTracy Yes refuses it even for a Debug tree' $debug $dirty @('-ForbidTracy', 'Yes') $true
    Test-Case '-ForbidTracy No allows it for a Release tree' $release $dirty @('-ForbidTracy', 'No') $false
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures -gt 0) {
    Write-Host "check_package forbid: $failures case(s) FAILED" -ForegroundColor Red
    exit 1
}
Write-Host 'check_package forbid: every case behaved'
exit 0
