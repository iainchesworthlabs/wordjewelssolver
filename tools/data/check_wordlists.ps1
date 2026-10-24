#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Holds the word lists under data/ to the rules the application relies on.
#
#   data/dictionaries/*.txt   one word per line, a-z only, lowercase, sorted
#                             (byte order), no duplicates.
#   data/lexicon/**/*.txt     the same, and every word must also be in
#                             data/dictionaries/en.txt: a category or tier is a
#                             subset of the dictionary, never a source of words
#                             the solver would not accept.
#
# -Fix sorts and de-duplicates the files in place. It never removes a word:
# a word that is not in the dictionary is reported and left for a person to
# correct, because "not a word" and "misspelt" are different mistakes.
#
# Exit: 0 = clean, 1 = violation(s), 2 = bad invocation.

param(
    [string]$Root = (Join-Path $PSScriptRoot '../..'),
    [switch]$Fix
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$dictionaryDir = Join-Path $Root 'data/dictionaries'
$lexiconDir = Join-Path $Root 'data/lexicon'
$english = Join-Path $dictionaryDir 'en.txt'

if (-not (Test-Path -LiteralPath $english)) {
    Write-Error "No data/dictionaries/en.txt under '$Root'."
    exit 2
}

function Read-Words([string]$path) {
    [string[]]$lines = [System.IO.File]::ReadAllLines($path)
    return $lines
}

$problems = [System.Collections.Generic.List[string]]::new()
function Add-Problem([string]$text) { $problems.Add($text) }

function Test-WordFile([string]$path, [System.Collections.Generic.HashSet[string]]$allowed) {
    $relative = [System.IO.Path]::GetRelativePath($Root, $path).Replace('\', '/')
    [string[]]$lines = Read-Words $path

    if ($Fix) {
        $clean = $lines | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ } |
            Sort-Object -Unique -CaseSensitive
        [System.IO.File]::WriteAllText($path, (($clean -join "`n") + "`n"), [System.Text.UTF8Encoding]::new($false))
        $lines = [string[]]$clean
    }

    $seen = [System.Collections.Generic.HashSet[string]]::new()
    $previous = $null
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $word = $lines[$i]
        $where = "${relative}:$($i + 1)"
        if ($word -cnotmatch '^[a-z]+$') {
            Add-Problem "$where  '$word' is not lowercase a-z"
            continue
        }
        if (-not $seen.Add($word)) { Add-Problem "$where  '$word' is listed twice" }
        if ($null -ne $previous -and [string]::CompareOrdinal($previous, $word) -gt 0) {
            Add-Problem "$where  '$word' is out of order (after '$previous')"
        }
        $previous = $word
        if ($null -ne $allowed -and -not $allowed.Contains($word)) {
            Add-Problem "$where  '$word' is not in data/dictionaries/en.txt"
        }
    }
    return $lines.Count
}

$total = 0
[string[]]$englishWords = Read-Words $english
$englishSet = [System.Collections.Generic.HashSet[string]]::new($englishWords)

$files = 0
foreach ($file in Get-ChildItem -LiteralPath $dictionaryDir -Filter *.txt -File) {
    $total += Test-WordFile $file.FullName $null
    $files++
}
if (Test-Path -LiteralPath $lexiconDir) {
    foreach ($file in Get-ChildItem -LiteralPath $lexiconDir -Recurse -Filter *.txt -File) {
        $total += Test-WordFile $file.FullName $englishSet
        $files++
    }
}

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host 'Word list problem(s):' -ForegroundColor Red
    foreach ($problem in $problems) {
        Write-Host "  $problem"
        $parts = $problem -split ':', 3
        Write-Host ("::error file={0},line={1}::{2}" -f $parts[0], ($parts[1] -split ' ')[0], $problem.Substring($problem.IndexOf(' ')).Trim())
    }
    Write-Host ''
    Write-Host ("{0} problem(s) in {1} file(s)." -f $problems.Count, $files) -ForegroundColor Red
    exit 1
}

Write-Host "OK: $files word list(s), $total entries, all lowercase a-z, sorted, unique, and every lexicon word is in the dictionary." -ForegroundColor Green
exit 0
