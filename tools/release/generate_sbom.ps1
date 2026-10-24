#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Write the CycloneDX 1.6 software bill of materials for a release.

.DESCRIPTION
    The SBOM states what the release packages contain and what built them,
    from the facts the repository already keeps rather than from a scan of the
    binaries (a scan of Qt's DLLs finds Qt, nothing more):

      * the application, wjs-cli and their licences: CMakeLists.txt's version,
        and notices/fragments (the application binary is GPL-3.0-only because
        it links Qt Quick 3D; the library and wjs-cli are GPL-3.0-or-later);
      * Qt, by module, at the version in .github/toolchain-versions.json and
        under the licence each module is used under - but only if a ZIP in
        -PackagesDir actually carries Qt6Core.dll, or a package is an AppImage, a
        disk image, an iOS IPA or an Android APK or AAB (which carry Qt by construction) (the Linux and macOS packages
        are command-line only today and say no Qt). Likewise the Microsoft
        Visual C++ runtime, if a ZIP carries vcruntime140.dll;
      * the data that ships: the ENABLE word list (its SHA-256 computed here,
        not copied from the notice) and each bundled font file with its
        SIL Open Font License 1.1;
      * what only built or tested it, with scope "excluded": the vcpkg commit,
        CMake and Ninja at their pinned versions, and every port vcpkg installed
        (its own vcpkg.spdx.json under -VcpkgSpdxDir, Catch2 today);
      * the release's own files as "distribution" references with their SHA-256,
        so the SBOM can be matched to the exact bytes it describes.

    It claims no more than that. Qt carries third-party code of its own; the
    document says so ("incomplete_third_party_only") and points to the SPDX
    files of the Qt kit, which are the authority for it. The output is
    deterministic for a given tree and tag: the serial number is derived from
    the tag and commit, and the timestamp is the commit's.

.PARAMETER Tag
    The release tag, vMAJOR.MINOR.PATCH[-prerelease].

.PARAMETER Commit
    The commit the release is built from. Default: HEAD of -Root.

.PARAMETER Repository
    owner/name, for the links. Default iainchesworthlabs/wordjewelssolver.

.PARAMETER PackagesDir
    Folder with the release's package files. Their names and hashes are listed;
    ZIPs are opened to see whether Qt and the C++ runtime are inside.

.PARAMETER VcpkgSpdxDir
    Folder searched recursively for vcpkg.spdx.json files (vcpkg writes one per
    installed port). Optional.

.PARAMETER Timestamp
    The document's time, ISO 8601. Default: the commit's own time (so the same
    release gives the same bytes), else now.

.PARAMETER Output
    The file to write.

.PARAMETER Root
    The repository checkout. Default: this script's repository.

.EXAMPLE
    ./tools/release/generate_sbom.ps1 -Tag v0.1.0 -PackagesDir dist -Output dist/WordJewelsSolver-0.1.0.cdx.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [string]$Commit,

    [string]$Repository = 'iainchesworthlabs/wordjewelssolver',

    [string]$PackagesDir,

    [string]$VcpkgSpdxDir,

    [string]$Timestamp,

    [Parameter(Mandatory = $true)]
    [string]$Output,

    [string]$Root
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $Root) { $Root = Join-Path $PSScriptRoot '../..' }
$Root = (Resolve-Path -LiteralPath $Root).Path

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

if ($Tag -notmatch '^v((0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))(-[0-9A-Za-z.-]+)?$') {
    throw "The tag '$Tag' is not vMAJOR.MINOR.PATCH[-prerelease]."
}
$version = $Tag.Substring(1)

if (-not $Commit) { $Commit = (git -C $Root rev-parse HEAD).Trim() }
if ($Timestamp) {
    $timestamp = $Timestamp
}
else {
    $timestamp = (git -C $Root show -s --format=%cI $Commit 2>$null)
    if ($LASTEXITCODE -ne 0 -or -not $timestamp) { $timestamp = [DateTimeOffset]::UtcNow.ToString('o') }
}
$timestamp = ([DateTimeOffset]::Parse($timestamp.Trim())).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")

# --- the facts ---------------------------------------------------------------

$toolchain = Get-Content -LiteralPath (Join-Path $Root '.github/toolchain-versions.json') -Raw | ConvertFrom-Json
$qtVersion = $toolchain.qt.version
$vcpkgPin = (Get-Content -LiteralPath (Join-Path $Root 'cmake/vcpkg-commit.txt') -TotalCount 1).Trim()

$dictionary = Join-Path $Root 'data/dictionaries/en.txt'
$fontFiles = @(Get-ChildItem -LiteralPath (Join-Path $Root 'assets/fonts') -Filter '*.ttf' -File | Sort-Object Name)

# The packages: names, sizes, hashes, and what a ZIP carries.
$packages = @()
$hasQt = $false
$hasVcRuntime = $false
if ($PackagesDir) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
    foreach ($file in (Get-ChildItem -LiteralPath $PackagesDir -File | Where-Object { $_.Extension -ne '.asc' -and $_.Name -ne 'SHA256SUMS' -and $_.Name -ne 'KEYS' -and $_.Name -notlike '*.cdx.json' -and $_.Name -notlike '*.spdx.json' } | Sort-Object Name)) {
        $packages += [pscustomobject]@{ Name = $file.Name; Sha256 = (Get-Sha256 $file.FullName); Size = $file.Length }
        # An AppImage, a disk image, an iOS IPA and an Android APK or AAB of the
        # application carry Qt by construction (linuxdeploy's Qt plugin, macOS's deploy
        # script, iOS's static Qt, androiddeployqt's bundled libraries) and are not
        # opened here.
        if ($file.Extension -in '.AppImage', '.dmg', '.ipa', '.apk', '.aab') { $hasQt = $true }
        if ($file.Extension -eq '.zip') {
            $zip = [System.IO.Compression.ZipFile]::OpenRead($file.FullName)
            try {
                foreach ($entry in $zip.Entries) {
                    if ($entry.Name -match '^Qt6Core(d)?\.dll$') { $hasQt = $true }
                    if ($entry.Name -match '^vcruntime140(_1)?\.dll$') { $hasVcRuntime = $true }
                }
            }
            finally { $zip.Dispose() }
        }
    }
}

# --- components --------------------------------------------------------------

function New-License([string]$Id) {
    return @{ license = [ordered]@{ id = $Id } }
}

$components = [System.Collections.Generic.List[object]]::new()
$appRef = "pkg:github/$Repository@$Tag"
$dependsOn = [System.Collections.Generic.List[string]]::new()

$components.Add([ordered]@{
        type        = 'application'
        'bom-ref'   = 'wjs-cli'
        name        = 'wjs-cli'
        version     = $version
        description = 'The command-line solver, built from the same tree and shipped in every desktop package.'
        licenses    = @(New-License 'GPL-3.0-or-later')
        purl        = "$appRef#wjs-cli"
    })

if ($hasQt) {
    # Which Qt module is used under which licence: notices/fragments/qt.txt and
    # qt-quick3d.txt. The repositories are Qt's own module names.
    $qtModules = @(
        @{ Name = 'qtbase'; Description = 'Qt Core and Qt Gui'; License = 'LGPL-3.0-only' },
        @{ Name = 'qtdeclarative'; Description = 'Qt Qml, Qt Quick and Qt Quick Controls 2'; License = 'LGPL-3.0-only' },
        @{ Name = 'qtmultimedia'; Description = 'Qt Multimedia (the camera preview and the feedback sounds)'; License = 'LGPL-3.0-only' },
        @{ Name = 'qtsensors'; Description = 'Qt Sensors (shake)'; License = 'LGPL-3.0-only' },
        @{ Name = 'qtspeech'; Description = 'Qt TextToSpeech'; License = 'LGPL-3.0-only' },
        @{ Name = 'qtquick3d'; Description = 'Qt Quick 3D (the board scene). Offered under GPL-3.0 or a commercial licence only, never the LGPL; it is why the application binary is GPL-3.0-only.'; License = 'GPL-3.0-only' }
    )
    foreach ($module in $qtModules) {
        $ref = "qt-$($module.Name)"
        $components.Add([ordered]@{
                type        = 'library'
                'bom-ref'   = $ref
                name        = $module.Name
                version     = $qtVersion
                description = $module.Description
                scope       = 'required'
                supplier    = [ordered]@{ name = 'The Qt Company Ltd.'; url = @('https://www.qt.io/') }
                licenses    = @(New-License $module.License)
                purl        = "pkg:generic/$($module.Name)@${qtVersion}?download_url=https://download.qt.io/archive/qt/$($qtVersion -replace '\.\d+$', '')/$qtVersion/submodules/"
                externalReferences = @(
                    [ordered]@{ type = 'website'; url = 'https://doc.qt.io/qt-6/licenses-used-in-qt.html'; comment = "Qt's own attribution for the code inside it" }
                )
            })
        $dependsOn.Add($ref)
    }
}

if ($hasVcRuntime) {
    $components.Add([ordered]@{
            type        = 'library'
            'bom-ref'   = 'msvc-runtime'
            name        = 'Microsoft Visual C++ Runtime'
            description = 'The compiler runtime the Qt deploy script copies beside the programs.'
            scope       = 'required'
            supplier    = [ordered]@{ name = 'Microsoft Corporation'; url = @('https://visualstudio.microsoft.com/') }
            licenses    = @(@{ license = [ordered]@{ name = 'Microsoft Visual Studio redistributable (Visual C++ runtime) terms' } })
        })
    $dependsOn.Add('msvc-runtime')
}

# The word list: the hash is computed from the file this release was built from.
$components.Add([ordered]@{
        type        = 'data'
        'bom-ref'   = 'enable-word-list'
        name        = 'ENABLE word list (enable1.txt)'
        description = 'dictionaries/en.txt, kept unmodified. Public domain as published with the list; the chain of title is not recorded (docs/licensing.md).'
        scope       = 'required'
        licenses    = @(@{ license = [ordered]@{ name = 'Public domain (ENABLE, as published with the list)' } })
        hashes      = @([ordered]@{ alg = 'SHA-256'; content = (Get-Sha256 $dictionary) })
        externalReferences = @(
            [ordered]@{ type = 'distribution'; url = 'https://raw.githubusercontent.com/dolph/dictionary/master/enable1.txt'; comment = 'Where data/README.md records it was fetched from' }
        )
    })
$dependsOn.Add('enable-word-list')

foreach ($font in $fontFiles) {
    $ref = "font-$($font.BaseName.ToLowerInvariant())"
    $components.Add([ordered]@{
            type        = 'data'
            'bom-ref'   = $ref
            name        = $font.Name
            description = 'Bundled interface font, compiled into the application as a Qt resource.'
            scope       = 'required'
            licenses    = @(New-License 'OFL-1.1')
            hashes      = @([ordered]@{ alg = 'SHA-256'; content = (Get-Sha256 $font.FullName) })
        })
    $dependsOn.Add($ref)
}

# What built and tested it, and is not in a package.
$components.Add([ordered]@{
        type        = 'application'
        'bom-ref'   = 'vcpkg'
        name        = 'vcpkg'
        version     = $vcpkgPin
        description = "The package manager the build used, at the commit in cmake/vcpkg-commit.txt (the labs' fork)."
        scope       = 'excluded'
    })
$components.Add([ordered]@{
        type = 'application'; 'bom-ref' = 'cmake'; name = 'CMake'; version = $toolchain.cmake
        description = 'Build system, pinned in .github/toolchain-versions.json.'; scope = 'excluded'
        licenses = @(New-License 'BSD-3-Clause')
    })
$components.Add([ordered]@{
        type = 'application'; 'bom-ref' = 'ninja'; name = 'Ninja'; version = $toolchain.ninja
        description = 'Build tool, pinned in .github/toolchain-versions.json.'; scope = 'excluded'
        licenses = @(New-License 'Apache-2.0')
    })

if ($VcpkgSpdxDir -and (Test-Path -LiteralPath $VcpkgSpdxDir)) {
    $seen = @{}
    foreach ($spdxFile in (Get-ChildItem -LiteralPath $VcpkgSpdxDir -Recurse -Filter 'vcpkg.spdx.json' -File | Sort-Object FullName)) {
        $spdx = Get-Content -LiteralPath $spdxFile.FullName -Raw | ConvertFrom-Json
        $port = @($spdx.packages)[0]
        if (-not $port -or -not $port.PSObject.Properties['name']) { continue }
        $key = "$($port.name)@$($port.versionInfo)"
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $licence = $null
        foreach ($field in 'licenseConcluded', 'licenseDeclared') {
            if ($port.PSObject.Properties[$field] -and $port.$field -and $port.$field -notin @('NOASSERTION', 'NONE')) { $licence = $port.$field; break }
        }
        $component = [ordered]@{
            type        = 'library'
            'bom-ref'   = "vcpkg-$($port.name)"
            name        = $port.name
            description = 'Installed by vcpkg to build or test; not in any package.'
            scope       = 'excluded'
            purl        = "pkg:vcpkg/$($port.name)@$($port.versionInfo)"
        }
        if ($port.PSObject.Properties['versionInfo'] -and $port.versionInfo) { $component['version'] = [string]$port.versionInfo }
        if ($licence) { $component['licenses'] = @(@{ expression = [string]$licence }) }
        $components.Add($component)
    }
}

# --- the document ------------------------------------------------------------

$serialSeed = [System.Text.Encoding]::UTF8.GetBytes("$Repository|$Tag|$Commit")
$serialHash = [System.BitConverter]::ToString([System.Security.Cryptography.SHA256]::HashData($serialSeed)).Replace('-', '').ToLowerInvariant()
$serial = "urn:uuid:$($serialHash.Substring(0, 8))-$($serialHash.Substring(8, 4))-$($serialHash.Substring(12, 4))-$($serialHash.Substring(16, 4))-$($serialHash.Substring(20, 12))"

$distribution = @()
foreach ($package in $packages) {
    $distribution += [ordered]@{
        type    = 'distribution'
        url     = "https://github.com/$Repository/releases/download/$Tag/$($package.Name)"
        comment = $package.Name
        hashes  = @([ordered]@{ alg = 'SHA-256'; content = $package.Sha256 })
    }
}
$references = @(
    [ordered]@{ type = 'vcs'; url = "https://github.com/$Repository"; comment = "Built from commit $Commit" },
    [ordered]@{ type = 'website'; url = "https://github.com/$Repository" },
    [ordered]@{ type = 'issue-tracker'; url = "https://github.com/$Repository/issues" },
    [ordered]@{ type = 'security-contact'; url = "https://github.com/$Repository/security/policy" }
) + $distribution

$dependsOnSorted = @($dependsOn | Sort-Object -Unique)
$application = [ordered]@{
    type               = 'application'
    'bom-ref'          = 'wordjewelssolver'
    name               = 'WordJewelsSolver'
    version            = $version
    description        = 'Finds every word on a Boggle board.'
    supplier           = [ordered]@{ name = 'Iain Chesworth'; url = @("https://github.com/$Repository") }
    licenses           = @(New-License 'GPL-3.0-only')
    purl               = $appRef
    externalReferences = $references
    properties         = @(
        [ordered]@{ name = 'wjs:commit'; value = $Commit },
        [ordered]@{ name = 'wjs:tracy'; value = 'off' },
        [ordered]@{ name = 'wjs:licence-note'; value = 'The window binary is GPL-3.0-only (it links Qt Quick 3D); the sources and wjs-cli are GPL-3.0-or-later.' }
    )
}

$document = [ordered]@{
    bomFormat    = 'CycloneDX'
    specVersion  = '1.6'
    serialNumber = $serial
    version      = 1
    metadata     = [ordered]@{
        timestamp  = $timestamp
        lifecycles = @(@{ phase = 'build' })
        tools      = [ordered]@{
            components = @([ordered]@{
                    type = 'application'; name = 'tools/release/generate_sbom.ps1'; version = $Commit
                    description = "This repository's SBOM generator."
                })
        }
        component  = $application
    }
    components   = @($components)
    dependencies = @(
        [ordered]@{ ref = 'wordjewelssolver'; dependsOn = @($dependsOnSorted) },
        [ordered]@{ ref = 'wjs-cli'; dependsOn = @() }
    )
    compositions = @([ordered]@{
            aggregate  = 'incomplete_third_party_only'
            assemblies = @('wordjewelssolver')
        })
}

$json = $document | ConvertTo-Json -Depth 12
$outputDirectory = Split-Path -Parent ([System.IO.Path]::GetFullPath($Output))
if ($outputDirectory) { New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null }
[System.IO.File]::WriteAllText([System.IO.Path]::GetFullPath($Output), $json + "`n", [System.Text.UTF8Encoding]::new($false))

Write-Host ("SBOM: {0} ({1} components, Qt {2}, C++ runtime {3}, {4} release file(s) referenced)" -f `
        $Output, $components.Count, $(if ($hasQt) { 'included' } else { 'not in these packages' }), $(if ($hasVcRuntime) { 'included' } else { 'not in these packages' }), $packages.Count)
