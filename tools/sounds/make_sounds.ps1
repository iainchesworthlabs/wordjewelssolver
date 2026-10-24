#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
    Writes the application's placeholder feedback sounds.

.DESCRIPTION
    The application's feedback table (docs/design/application.md, "Audio and
    touch feedback") names short effects: a rattle, a die landing, a letter
    joining, a tick, a cheer, a chime, the cleared notes and the tap of a
    control. The real phoneme clips and the recorded cheers are the reviewer's
    recording job (docs/design/phonics.md); until then these are synthesised
    placeholders, clearly labelled as such, so the wiring and the settings can
    be heard and tested. They are the project's own work, not third-party
    audio, and notices/fragments/assets.txt says so.

    The files are 16-bit PCM mono WAV at 22050 Hz: small, and every platform's
    decoder reads them. They are deterministic: the same script writes the same
    bytes, so -Check can hold the checked-in files to it (the data.sounds test
    does, in the same way data.dice is held to make_dice.ps1).

.PARAMETER Root
    The repository root. Defaults to this script's checkout.

.PARAMETER Check
    Write nothing; fail if a checked-in file differs from what would be written.
    A missing file is a failure too.

.EXAMPLE
    ./tools/sounds/make_sounds.ps1
    ./tools/sounds/make_sounds.ps1 -Check
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$SampleRate = 22050
$pi = [Math]::PI

function New-Buffer([double]$seconds) {
    return [double[]]::new([int][Math]::Round($seconds * $SampleRate))
}

# A deterministic white noise in [-1, 1): the same bytes on every run and every
# machine, so a checked-in file can be reproduced.
$script:noiseState = [uint32]0x5EED1234
function Next-Noise {
    $script:noiseState = [uint32](([uint64]1664525 * [uint64]$script:noiseState + 1013904223) % 4294967296)
    return ([double]$script:noiseState / 2147483648.0) - 1.0
}

# Adds a decaying sine to the buffer: a quick attack so a click has its edge,
# then an exponential fall that ends before the file does.
function Add-Tone([double[]]$buffer, [double]$start, [double]$seconds, [double]$frequency,
                   [double]$amplitude) {
    $from = [int][Math]::Round($start * $SampleRate)
    $count = [int][Math]::Round($seconds * $SampleRate)
    for ($i = 0; $i -lt $count; $i++) {
        $at = $from + $i
        if ($at -ge $buffer.Length) { break }
        $t = $i / $SampleRate
        $attack = [Math]::Min(1.0, $t / 0.004)
        $release = [Math]::Exp(-3.5 * $t / $seconds)
        $buffer[$at] += $amplitude * $attack * $release * [Math]::Sin(2.0 * $pi * $frequency * $t)
    }
}

function Add-Noise([double[]]$buffer, [double]$start, [double]$seconds, [double]$amplitude) {
    $from = [int][Math]::Round($start * $SampleRate)
    $count = [int][Math]::Round($seconds * $SampleRate)
    for ($i = 0; $i -lt $count; $i++) {
        $at = $from + $i
        if ($at -ge $buffer.Length) { break }
        $t = $i / $SampleRate
        $attack = [Math]::Min(1.0, $t / 0.002)
        $release = [Math]::Exp(-4.0 * $t / $seconds)
        $buffer[$at] += $amplitude * $attack * $release * (Next-Noise)
    }
}

function New-Tap {
    $buffer = New-Buffer 0.045
    Add-Tone $buffer 0.0 0.045 950 0.35
    return $buffer
}

function New-Rattle {
    $buffer = New-Buffer 0.42
    $starts = @(0.0, 0.055, 0.115, 0.17, 0.225, 0.285, 0.34)
    $levels = @(0.22, 0.3, 0.24, 0.32, 0.25, 0.3, 0.2)
    for ($i = 0; $i -lt $starts.Count; $i++) {
        Add-Noise $buffer $starts[$i] 0.05 $levels[$i]
        Add-Tone $buffer $starts[$i] 0.03 (520 + 35 * $i) 0.08
    }
    return $buffer
}

function New-Letter {
    $buffer = New-Buffer 0.16
    Add-Tone $buffer 0.0 0.16 660 0.3
    Add-Tone $buffer 0.0 0.12 990 0.08
    return $buffer
}

function New-Land {
    $buffer = New-Buffer 0.07
    Add-Tone $buffer 0.0 0.07 1180 0.22
    Add-Tone $buffer 0.0 0.05 590 0.12
    Add-Noise $buffer 0.0 0.015 0.08
    return $buffer
}

function New-Cheer {
    $buffer = New-Buffer 0.62
    Add-Tone $buffer 0.0 0.22 523.25 0.24
    Add-Tone $buffer 0.14 0.22 659.25 0.24
    Add-Tone $buffer 0.28 0.30 783.99 0.28
    Add-Tone $buffer 0.28 0.30 1046.50 0.08
    return $buffer
}

function New-Tick {
    $buffer = New-Buffer 0.1
    Add-Tone $buffer 0.0 0.1 330 0.2
    return $buffer
}

function New-Chime {
    $buffer = New-Buffer 0.55
    Add-Tone $buffer 0.0 0.55 880 0.2
    Add-Tone $buffer 0.0 0.45 1320 0.1
    Add-Tone $buffer 0.0 0.32 1760 0.05
    return $buffer
}

function New-Cleared {
    $buffer = New-Buffer 0.95
    Add-Tone $buffer 0.0 0.3 523.25 0.2
    Add-Tone $buffer 0.28 0.3 659.25 0.2
    Add-Tone $buffer 0.56 0.38 783.99 0.22
    return $buffer
}

function ConvertTo-Wav([double[]]$samples) {
    $dataLength = $samples.Length * 2
    $bytes = [byte[]]::new(44 + $dataLength)
    $ascii = [System.Text.Encoding]::ASCII
    [Array]::Copy($ascii.GetBytes('RIFF'), 0, $bytes, 0, 4)
    [Array]::Copy([BitConverter]::GetBytes([int](36 + $dataLength)), 0, $bytes, 4, 4)
    [Array]::Copy($ascii.GetBytes('WAVE'), 0, $bytes, 8, 4)
    [Array]::Copy($ascii.GetBytes('fmt '), 0, $bytes, 12, 4)
    [Array]::Copy([BitConverter]::GetBytes([int]16), 0, $bytes, 16, 4)
    [Array]::Copy([BitConverter]::GetBytes([int16]1), 0, $bytes, 20, 2)   # PCM
    [Array]::Copy([BitConverter]::GetBytes([int16]1), 0, $bytes, 22, 2)   # mono
    [Array]::Copy([BitConverter]::GetBytes([int]$SampleRate), 0, $bytes, 24, 4)
    [Array]::Copy([BitConverter]::GetBytes([int]($SampleRate * 2)), 0, $bytes, 28, 4)
    [Array]::Copy([BitConverter]::GetBytes([int16]2), 0, $bytes, 32, 2)   # block align
    [Array]::Copy([BitConverter]::GetBytes([int16]16), 0, $bytes, 34, 2)  # bits
    [Array]::Copy($ascii.GetBytes('data'), 0, $bytes, 36, 4)
    [Array]::Copy([BitConverter]::GetBytes([int]$dataLength), 0, $bytes, 40, 4)
    for ($i = 0; $i -lt $samples.Length; $i++) {
        $value = [Math]::Max(-0.95, [Math]::Min(0.95, $samples[$i]))
        $pcm = [int16][Math]::Round($value * 32767.0)
        [Array]::Copy([BitConverter]::GetBytes($pcm), 0, $bytes, 44 + 2 * $i, 2)
    }
    return $bytes
}

# Declared in the order they are written, so the directory listing is stable.
$sounds = [ordered]@{
    tap     = New-Tap
    rattle  = New-Rattle
    letter  = New-Letter
    land    = New-Land
    cheer   = New-Cheer
    tick    = New-Tick
    chime   = New-Chime
    cleared = New-Cleared
}

$directory = Join-Path $Root 'assets/sounds'
if (-not (Test-Path -LiteralPath $directory)) {
    if ($Check) {
        Write-Host "FAILED: $directory does not exist (run tools/sounds/make_sounds.ps1)." -ForegroundColor Red
        exit 1
    }
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$failures = 0
foreach ($name in $sounds.Keys) {
    $path = Join-Path $directory "$name.wav"
    $bytes = ConvertTo-Wav $sounds[$name]
    if ($Check) {
        if (-not (Test-Path -LiteralPath $path)) {
            Write-Host "MISSING $path" -ForegroundColor Red
            $failures++
            continue
        }
        $existing = [System.IO.File]::ReadAllBytes($path)
        $same = $existing.Length -eq $bytes.Length
        if ($same) {
            for ($i = 0; $i -lt $bytes.Length; $i++) {
                if ($existing[$i] -ne $bytes[$i]) {
                    $same = $false
                    break
                }
            }
        }
        if (-not $same) {
            Write-Host "DIFFERS $path (regenerate with tools/sounds/make_sounds.ps1)" -ForegroundColor Red
            $failures++
        }
    }
    else {
        [System.IO.File]::WriteAllBytes($path, $bytes)
        Write-Host ("      wrote {0} ({1} bytes)" -f $path, $bytes.Length)
    }
}

if ($Check) {
    if ($failures -gt 0) {
        Write-Host "FAILED: $failures placeholder sound(s) are not what the generator writes." -ForegroundColor Red
        exit 1
    }
    Write-Host "OK: the $($sounds.Count) placeholder sounds are what the generator writes."
}
