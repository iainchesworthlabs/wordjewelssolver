#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Check that a Release build and its packages are what a release may ship.

.DESCRIPTION
    Run after cpack, on the build tree and its packages/ directory:

      1. the profiler is off. CMakeCache.txt says WJS_ENABLE_TRACY:BOOL=OFF, and
         no file inside any package that can be listed has Tracy in its name
         (TracyClient.dll, libTracyClient.so). A release with the profiler in
         it opens a network port (docs/profiling.md);
      2. the build type is Release, the sanitizers are off in the cache (the
         Release presets set WJS_ENABLE_SANITIZERS=OFF, so no sanitizer runtime is
         copied into the tree), and no package that can be listed holds a
         sanitizer runtime (an asan or clang_rt file in a package is a Debug
         build shipped by mistake);
      3. the packages that must exist for this platform do: -Expect names file
         suffixes, e.g. .zip, .exe, and each must match exactly one file.

    ZIP, tar.gz and deb are opened and listed; the rest (a disk image, an
    installer, an rpm) are checked by name only, and the cache and the ZIP from
    the same install tree cover what is inside.

.PARAMETER Build
    The configured build tree.

.PARAMETER Expect
    File name suffixes that must each match exactly one package, such as
    '.zip','.exe'.

.EXAMPLE
    ./tools/release/check_release_build.ps1 -Build build/linux-gcc-release -Expect .zip,.tar.gz,.deb,.rpm
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Build,

    [Parameter(Mandatory = $true)]
    [string[]]$Expect
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Fail([string]$text) {
    Write-Host "FAILED: $text" -ForegroundColor Red
    if ($env:GITHUB_ACTIONS) { Write-Host "::error::$text" }
    exit 1
}

$buildDirectory = (Resolve-Path -LiteralPath $Build).Path
$cache = Join-Path $buildDirectory 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache)) { Fail "$cache does not exist: not a configured build tree." }
$cacheText = Get-Content -LiteralPath $cache -Raw

function Get-CacheValue([string]$name) {
    if ($cacheText -match "(?m)^$([regex]::Escape($name)):[A-Z]+=(.*)$") { return $Matches[1].Trim() }
    return $null
}

# 1 and 2. The configuration.
$tracy = Get-CacheValue 'WJS_ENABLE_TRACY'
if ($tracy -ne 'OFF') { Fail "WJS_ENABLE_TRACY is '$tracy' in $cache; a release is built with the profiler off." }
$type = Get-CacheValue 'CMAKE_BUILD_TYPE'
if ($type -ne 'Release') { Fail "CMAKE_BUILD_TYPE is '$type'; a release is built as Release." }
$sanitizers = Get-CacheValue 'WJS_ENABLE_SANITIZERS'
if ($sanitizers -ne 'OFF') { Fail "WJS_ENABLE_SANITIZERS is '$sanitizers' in $cache; the Release presets turn it off." }
Write-Host '    configuration: Release, Tracy off, sanitizers off'

# 3. The packages.
$packagesDirectory = Join-Path $buildDirectory 'packages'
$files = @(Get-ChildItem -LiteralPath $packagesDirectory -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ne 'SHA256SUMS' })
foreach ($suffix in $Expect) {
    $matching = @($files | Where-Object { $_.Name.EndsWith($suffix, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($matching.Count -eq 0) { Fail "no package ending $suffix in $packagesDirectory (found: $(($files | ForEach-Object Name) -join ', '))." }
    if ($matching.Count -gt 1) { Fail "more than one package ends $suffix in ${packagesDirectory}: $(($matching | ForEach-Object Name) -join ', ')." }
    Write-Host ("    package: {0} ({1:N1} MiB)" -f $matching[0].Name, ($matching[0].Length / 1MB))
}

# Tracy inside what can be listed.
Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
foreach ($file in $files) {
    $entries = @()
    switch -Regex ($file.Name) {
        '\.zip$' {
            $zip = [System.IO.Compression.ZipFile]::OpenRead($file.FullName)
            try { $entries = @($zip.Entries | ForEach-Object { $_.FullName }) } finally { $zip.Dispose() }
        }
        '\.tar\.gz$' { $entries = @(& tar -tzf $file.FullName) }
        '\.deb$' { $entries = @(& dpkg-deb -c $file.FullName) }
    }
    $hit = @($entries | Where-Object { $_ -match '(?i)tracy' })
    if ($hit.Count -gt 0) { Fail "$($file.Name) contains the profiler: $($hit -join ', ')" }
    $runtime = @($entries | Where-Object { $_ -match '(?i)(asan|ubsan|clang_rt)' })
    if ($runtime.Count -gt 0) { Fail "$($file.Name) contains a sanitizer runtime: $($runtime -join ', ')" }
    if ($entries.Count -gt 0) { Write-Host "    listed $($file.Name): $($entries.Count) entries, no Tracy, no sanitizer runtime" }
}

Write-Host 'release build: ok' -ForegroundColor Green
