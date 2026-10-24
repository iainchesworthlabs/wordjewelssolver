#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Run tests/package/test_release_tools.sh where a POSIX shell and its tools exist.

.DESCRIPTION
    The self-test of the release tooling needs bash, gpg, jq, git and pwsh. This
    wrapper is what ctest runs (release.tooling): it finds a bash that is really
    a shell (on Windows, Git for Windows' - not the WSL launcher in System32,
    which a PATH lookup can find first), and exits 77, which the test declares
    its skip code, when there is none or the script reports that a tool is
    missing (exit 2). Any other non-zero exit is a failure.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'test_release_tools.sh'

$bash = $null
if ($IsWindows) {
    # Git for Windows keeps bash beside git: walk up from git.exe to the folder
    # that has bin/bash.exe (git.exe is in cmd/ or mingw64/bin/).
    $git = Get-Command git -ErrorAction SilentlyContinue
    $directory = if ($git) { Split-Path -Parent $git.Source } else { $null }
    while ($directory -and -not $bash) {
        $candidate = Join-Path $directory 'bin/bash.exe'
        if (Test-Path -LiteralPath $candidate) { $bash = $candidate }
        $directory = Split-Path -Parent $directory
    }
}
else {
    $command = Get-Command bash -ErrorAction SilentlyContinue
    if ($command) { $bash = $command.Source }
}
if (-not $bash) {
    Write-Host 'SKIP: no bash found'
    exit 77
}

# bash is given the script as a path it understands.
$path = if ($IsWindows) { $script.Replace([string][char]92, '/') } else { $script }
& $bash $path
$code = $LASTEXITCODE
if ($code -eq 2) {
    Write-Host 'SKIP: a tool the self-test needs (gpg, jq, git or pwsh) is not available'
    exit 77
}
exit $code
