#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Make a package with CPack and check that it is one: its contents, the
    packaged command-line solver, and the packaged window's headless smoke.

.DESCRIPTION
    The install rules and the CPack configuration are checked at configure time
    by the package.config ctest, which needs no build. This script is the other
    half: it packages a built tree and runs what came out.

      1. cpack -G <generator> in the build tree (skipped with -Archive), which
         writes the package to <build>/packages/;
      2. lists the archive, its size and its required entries;
      3. writes SHA256SUMS beside it, in sha256sum's format;
      4. unpacks it and runs the packaged wjs-cli on a board with no -d, so the
         dictionary beside the program is the one it uses;
      5. fails if the package holds a development leftover - a TracyClient
         library, an AddressSanitizer runtime (clang_rt.asan*) or a .pdb - which
         a Release package must never carry (-ForbidTracy, on by default for a
         Release build tree);
      6. runs the packaged WordJewelsSolver with the offscreen platform plugin,
         so a Windows runner with no display proves the installed tree carries
         Qt's runtime, its QML modules and the Quick 3D plugin (skip with
         -NoSmoke).

    Windows is where the ZIP is checked, because it is the package CI builds
    and the one the Qt deploy script makes self-contained (docs/ci.md). The
    Linux tarball and the distribution packages are not read here: a package
    whose layout is not the ZIP's would need its own check, and no leg builds
    one yet.

    Nothing here reaches the network. The unpacked tree is left under
    <build>/package-check/ for a look with -Keep, and removed otherwise.

.PARAMETER Build
    The configured build tree, e.g. build/windows-msvc-release. It must have
    CPackConfig.cmake, so it must have been configured with this repository's
    CMakeLists.txt since packaging was added.

.PARAMETER Generator
    The CPack generator to run. ZIP by default.

.PARAMETER Archive
    Check this archive instead of running cpack.

.PARAMETER ForbidTracy
    Auto (the default): fail on TracyClient*, clang_rt.asan* and *.pdb in the
    package when the build tree is a Release one, and let cpack refuse a tree
    that has Tracy in it. Yes: always. No: a development package (a Debug tree
    packaged to check the packaging itself): cpack is told so with
    -D CPACK_WJS_ALLOW_DEV_PACKAGE=ON, and the package may carry the profiler.

.PARAMETER NoSmoke
    Do not run the packaged application. For a host with no way to run it.

.PARAMETER Keep
    Leave the unpacked package in <build>/package-check/.

.EXAMPLE
    ./tools/package/check_package.ps1 -Build build/windows-msvc-release

.EXAMPLE
    cpack -G ZIP --config build/windows-msvc-release/CPackConfig.cmake
    ./tools/package/check_package.ps1 -Build build/windows-msvc-release -Archive build/windows-msvc-release/packages/WordJewelsSolver-0.1.0-win64.zip
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Build,

    [string]$Generator = 'ZIP',

    [string]$Archive,

    [ValidateSet('Auto', 'Yes', 'No')]
    [string]$ForbidTracy = 'Auto',

    [switch]$NoSmoke,

    [switch]$Keep
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step([string]$text) {
    Write-Host "==> $text" -ForegroundColor Cyan
}

function Fail([string]$text) {
    Write-Host "FAILED: $text" -ForegroundColor Red
    exit 1
}

$buildDirectory = (Resolve-Path -LiteralPath $Build).Path
$cpackConfig = Join-Path $buildDirectory 'CPackConfig.cmake'
if (-not (Test-Path -LiteralPath $cpackConfig)) {
    Fail "$cpackConfig does not exist. Configure the tree first (docs/building.md, 'Packaging')."
}
$packagesDirectory = Join-Path $buildDirectory 'packages'

# Release or development: a Release tree must make a clean package, so the check
# is on for it; a Debug tree is packaged only to check the packaging, and is a
# development package that cpack has to be told it may make.
$buildType = ''
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $typeLine = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_BUILD_TYPE:[A-Z]+=(.*)$' | Select-Object -First 1
    if ($typeLine) { $buildType = $typeLine.Matches[0].Groups[1].Value.Trim() }
}
$forbid = switch ($ForbidTracy) {
    'Yes' { $true }
    'No' { $false }
    default { $buildType -eq 'Release' }
}

# ---------------------------------------------------------------------------
# 1. Package, unless an archive was named.
# ---------------------------------------------------------------------------
if ($Generator -ne 'ZIP') {
    Fail ("this check reads the ZIP package (its contents, and the programs run from " +
        "the unpacked tree). cpack -G $Generator still makes the other formats - see " +
        "docs/building.md, 'Packaging' - but check one of those by hand.")
}
if ($Archive -and [System.IO.Path]::GetExtension($Archive) -ne '.zip') {
    Fail "-Archive must name a ZIP (this check does not read another format)."
}

if (-not $Archive) {
    Write-Step "cpack -G $Generator in $buildDirectory"
    Push-Location -LiteralPath $buildDirectory
    try {
        $cpackArguments = @('-G', $Generator, '--config', $cpackConfig)
        if (-not $forbid) { $cpackArguments += @('-D', 'CPACK_WJS_ALLOW_DEV_PACKAGE=ON') }
        & cpack @cpackArguments
        if ($LASTEXITCODE -ne 0) {
            Fail "cpack exited $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    $found = @(Get-ChildItem -LiteralPath $packagesDirectory -Filter '*.zip' -File |
        Sort-Object LastWriteTime -Descending)
    if ($found.Count -eq 0) {
        Fail "cpack wrote no .zip in $packagesDirectory"
    }
    $Archive = $found[0].FullName
}
else {
    $Archive = (Resolve-Path -LiteralPath $Archive).Path
}

$archiveFile = Get-Item -LiteralPath $Archive
Write-Step "Package: $($archiveFile.FullName) ($([math]::Round($archiveFile.Length / 1MB, 1)) MiB)"

# SHA256SUMS beside the package, in the format sha256sum writes, so a release
# can publish it without another pass.
$checksumLine = "$((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant())  $($archiveFile.Name)"
$checksumsPath = Join-Path $archiveFile.Directory.FullName 'SHA256SUMS'
Set-Content -LiteralPath $checksumsPath -Value $checksumLine -Encoding utf8NoBOM
Write-Host "    SHA256SUMS: $checksumLine"

# ---------------------------------------------------------------------------
# 2. What is inside.
# ---------------------------------------------------------------------------
Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
$zip = [System.IO.Compression.ZipFile]::OpenRead($archiveFile.FullName)
try {
    $entries = @($zip.Entries | ForEach-Object {
            [pscustomobject]@{ FullName = $_.FullName; Name = $_.Name; Length = $_.Length }
        })
}
finally {
    $zip.Dispose()
}
$files = @($entries | Where-Object { $_.Name -ne '' })
$uncompressed = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ("    {0} files, {1:N1} MiB uncompressed" -f $files.Count, ($uncompressed / 1MB))
Write-Host ''
Write-Host '    The package:'
Get-ChildItem -LiteralPath $archiveFile.Directory.FullName -File | Sort-Object Name |
    Format-Table Name, Length -AutoSize | Out-String | Write-Host

# Every required path is checked by its suffix, so a package whose contents
# are under a top-level directory and one that is flat both pass: CPack's
# archives include the top-level directory (cmake/Packaging.cmake).
$paths = @($files | ForEach-Object { $_.FullName })
function Test-Packaged([string]$relative) {
    foreach ($path in $paths) {
        if ($path -eq $relative -or $path.EndsWith('/' + $relative)) {
            return $true
        }
    }
    return $false
}
function Test-PackagedDirectory([string]$relative) {
    foreach ($path in $paths) {
        if ($path.Contains('/' + $relative + '/') -or $path.StartsWith($relative + '/')) {
            return $true
        }
    }
    return $false
}

# What a Release package must not carry: the profiler's client library, an
# AddressSanitizer runtime (a Debug build shipped by mistake) and debug symbols.
if ($forbid) {
    $forbiddenPattern = '(?i)(^|/)(TracyClient[^/]*|clang_rt\.asan[^/]*|[^/]*\.pdb)$'
    $forbiddenFound = @($paths | Where-Object { $_ -match $forbiddenPattern })
    if ($forbiddenFound.Count -gt 0) {
        Fail "the package holds a development leftover a Release package must not: $($forbiddenFound -join ', ')"
    }
    Write-Host '    no TracyClient, AddressSanitizer runtime or .pdb in the package'
}
else {
    Write-Host "    -ForbidTracy is off for this tree ($buildType): a development package, which may carry the profiler"
}

$executableSuffix = if ($IsWindows) { '.exe' } else { '' }
$required = @(
    "bin/wjs-cli$executableSuffix",
    'bin/dictionaries/en.txt',
    'bin/NOTICES.txt',
    'bin/LICENSE.txt'
)
# The window and what it reads: only a tree that was configured with the
# application has them.
$withApplication = Test-Packaged "bin/WordJewelsSolver$executableSuffix"
if ($withApplication) {
    $required += @(
        'bin/dice/3x3.txt',
        'bin/dice/4x4.txt',
        'bin/dice/5x5.txt',
        'bin/dice/6x6.txt'
    )
    if ($IsWindows) {
        # Qt's runtime, the platform plugins (the offscreen one is what the
        # smoke below runs with) and the QML modules the window imports,
        # Quick 3D's among them. The DLLs are beside the program; Qt's deploy
        # script puts the plugins and QML modules at the install prefix, one
        # level above bin, and writes the qt.conf that points there.
        #
        # A Debug build deploys the debug DLLs (Qt6Cored.dll, qwindowsd.dll):
        # the suffix is the configuration's, so the same check reads a package
        # from either preset.
        $qtSuffix = if (Test-Path -LiteralPath (Join-Path $buildDirectory 'bin/Qt6Cored.dll')) { 'd' } else { '' }
        $required += @(
            "bin/Qt6Core$qtSuffix.dll",
            "bin/Qt6Qml$qtSuffix.dll",
            "bin/Qt6Quick$qtSuffix.dll",
            "bin/Qt6Quick3D$qtSuffix.dll",
            'bin/qt.conf',
            "plugins/platforms/qwindows$qtSuffix.dll",
            "plugins/platforms/qoffscreen$qtSuffix.dll"
        )
        # The feedback engines are optional at configure time (their silent
        # variants), so their DLLs are required only when the build beside this
        # package deployed them: windeployqt copies a DLL a linked module needs,
        # and an app built without the module has no such import. A build that
        # links one but whose package lacks it is caught by the packaged
        # window's smoke below.
        foreach ($module in @('Qt6Multimedia', 'Qt6Sensors', 'Qt6TextToSpeech')) {
            if (Test-Path -LiteralPath (Join-Path $buildDirectory "bin/$module$qtSuffix.dll")) {
                $required += "bin/$module$qtSuffix.dll"
            }
        }
    }
}

Write-Host '    Required entries:'
$missing = @()
foreach ($relative in $required) {
    if (Test-Packaged $relative) {
        Write-Host "      ok      $relative"
    }
    else {
        Write-Host "      MISSING $relative" -ForegroundColor Red
        $missing += $relative
    }
}
if ($withApplication -and $IsWindows) {
    $requiredDirectories = @('qml/QtQuick', 'qml/QtQuick3D')
    # The scanner's live view is Qt Multimedia's VideoOutput, and its camera is a
    # Multimedia backend plugin: when the window links the module, the package
    # must carry both, or the scan screen starts and cannot show a preview. The
    # Qt deploy scripts copy the plugins and the QML module because the
    # application imports QtMultimedia (apps/app/src/camera/CameraImport.qml).
    if (Test-Path -LiteralPath (Join-Path $buildDirectory "bin/Qt6Multimedia$qtSuffix.dll")) {
        $requiredDirectories += @('plugins/multimedia', 'qml/QtMultimedia')
    }
    foreach ($directory in $requiredDirectories) {
        if (Test-PackagedDirectory $directory) {
            Write-Host "      ok      $directory/"
        }
        else {
            Write-Host "      MISSING $directory/" -ForegroundColor Red
            $missing += "$directory/"
        }
    }
}
if ($missing.Count -gt 0) {
    Fail "the package is missing: $($missing -join ', ')"
}

# ---------------------------------------------------------------------------
# 3. Unpack, and run what came out.
# ---------------------------------------------------------------------------
$checkRoot = Join-Path $buildDirectory 'package-check'
$extract = Join-Path $checkRoot $archiveFile.BaseName
if (-not $extract.StartsWith($buildDirectory, [System.StringComparison]::OrdinalIgnoreCase)) {
    Fail "refusing to remove '$extract': it is not under $buildDirectory"
}
if (Test-Path -LiteralPath $extract) {
    Remove-Item -LiteralPath $extract -Recurse -Force
}
New-Item -ItemType Directory -Path $extract -Force | Out-Null
Write-Step "Unpacking into $extract"
[System.IO.Compression.ZipFile]::ExtractToDirectory($archiveFile.FullName, $extract)

$cli = @(Get-ChildItem -LiteralPath $extract -Recurse -File -Filter "wjs-cli$executableSuffix")[0]
if (-not $cli) {
    Fail "no wjs-cli$executableSuffix in the unpacked package"
}
$binDirectory = $cli.Directory.FullName
Write-Host "    programs: $binDirectory"

# The solver, with the dictionary the package installed beside it: no -d, so a
# missing or stale dictionaries/en.txt is a failure and not a silent fallback.
Write-Step 'The packaged wjs-cli solves a board with its own dictionary'
$cliOutput = & $cli.FullName CAT ORE DGS 2>&1
$cliExit = $LASTEXITCODE
$cliOutput | Select-Object -First 12 | ForEach-Object { Write-Host "    $_" }
if ($cliExit -ne 0) {
    Fail "the packaged wjs-cli exited $cliExit"
}
foreach ($word in @('CAT', 'CARE', 'TAR')) {
    if (-not ($cliOutput -match "(?m)^$word\s")) {
        Fail "the packaged wjs-cli did not find $word using dictionaries/en.txt"
    }
}

$application = Join-Path $binDirectory "WordJewelsSolver$executableSuffix"
if (-not (Test-Path -LiteralPath $application)) {
    Write-Host '    No application in this package: the offscreen smoke is skipped.'
}
elseif ($NoSmoke) {
    Write-Host '    -NoSmoke: the packaged application is not run.'
}
else {
    Write-Step 'The packaged WordJewelsSolver starts and presents frames, offscreen'
    # The store in the unpacked tree, so the check leaves the profile alone.
    $store = Join-Path $extract 'smoke-store.txt'
    $previous = @{}
    foreach ($name in @('QT_QPA_PLATFORM', 'QSG_RHI_BACKEND', 'QT_QPA_FONTDIR')) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        # Quick 3D needs no display for the start-up path and the 2D graph;
        # the offscreen plugin draws text with the system's fonts on Windows.
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QSG_RHI_BACKEND = 'opengl'
        if ($IsWindows) {
            $env:QT_QPA_FONTDIR = Join-Path $env:SystemRoot 'Fonts'
        }
        # Start-Process, not &-and-wait: on Windows the application is a
        # GUI-subsystem program, and PowerShell does not wait for one or capture
        # its output. The redirections are inherited handles, so the frame report
        # on stdout is caught either way.
        $smokeOut = Join-Path $extract 'smoke-stdout.txt'
        $smokeErr = Join-Path $extract 'smoke-stderr.txt'
        $smokeArguments = @('--smoke', '10', '--store', $store)
        $process = Start-Process -FilePath $application -ArgumentList $smokeArguments `
            -NoNewWindow -Wait -PassThru `
            -RedirectStandardOutput $smokeOut -RedirectStandardError $smokeErr
        $smokeExit = $process.ExitCode
        $smokeOutput = @()
        foreach ($stream in @($smokeOut, $smokeErr)) {
            if (Test-Path -LiteralPath $stream) {
                $smokeOutput += @(Get-Content -LiteralPath $stream)
            }
        }
    }
    finally {
        foreach ($name in $previous.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }
    $smokeOutput | ForEach-Object { Write-Host "    $_" }
    if ($smokeExit -ne 0) {
        Fail "the packaged WordJewelsSolver --smoke 10 exited $smokeExit"
    }
    if (-not ($smokeOutput -match 'smoke: frames=(\d+)')) {
        Fail 'the packaged WordJewelsSolver printed no frame report'
    }
}

if (-not $Keep) {
    Remove-Item -LiteralPath $extract -Recurse -Force
    if ((Test-Path -LiteralPath $checkRoot) -and
        -not (Get-ChildItem -LiteralPath $checkRoot -Force)) {
        Remove-Item -LiteralPath $checkRoot -Force
    }
}

Write-Host ''
Write-Host "package: ok - $($archiveFile.Name), SHA256SUMS written" -ForegroundColor Green
exit 0
