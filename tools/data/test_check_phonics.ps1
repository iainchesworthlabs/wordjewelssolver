#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Self-test for check_phonics.ps1: a checker that silently passes everything is
# worse than none, and it is the build's review gate. Each case builds a
# throw-away repository root (a small dictionary and some phonics files), runs
# the checker over it and looks at the exit code and what it said. The cases
# are the ones libs/core/tests/unit/phase_data_test.cpp holds the library's reader to, so
# the two implementations are held to the same table.
#
# TEST DATA. The words, graphemes and sounds below are written for this test:
# the words are ones the product brief itself names, the sounds are placeholders
# (/t1/), and none of it is content for a child. No file under data/phonics is
# read or written.
#
# Usage:  ./tools/data/test_check_phonics.ps1
# Exit:   0 = all cases behaved, 1 = at least one did not.

$ErrorActionPreference = 'Stop'

$checker = Join-Path $PSScriptRoot 'check_phonics.ps1'
$pwsh = (Get-Process -Id $PID).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path

$dictionaryWords = 'nip', 'pan', 'pat', 'pin', 'sat', 'sit', 'tap', 'pain', 'paint', 'saint', 'stain', 'xyz',
    'cake', 'like', 'snow', 'cow'

$gradeLine = '# reviewed-by: Test Person   reviewed-on: 2026-10-20   source: written in the test'

$graphemesA = @'
a  /t1/  clip:t1
i  /t2/  clip:t2
n  /t3/  clip:t3
p  /t4/  clip:t4
s  /t5/  clip:t5
t  /t6/  clip:t6
'@

$graphemesB = 'ai  /t7/  clip:t7'

$wordsA = @'
nip  n i p
pan  p a n
pat  p a t
pin  p i n
sat  s a t
sit  s i t
tap  t a p
'@

$wordsB = @'
pain  p ai n
paint  p ai n t
saint  s ai n t
stain  s t ai n
'@

# A file's text: its phase line, optionally the review line, then the body.
function New-File([string]$Phase, [string]$Body, [switch]$Reviewed) {
    $text = "# phase: $Phase`n"
    if ($Reviewed) { $text += "$gradeLine`n" }
    return $text + $Body + "`n"
}

# The two phases, as a table of file name -> text.
function New-Phases([switch]$Reviewed) {
    return [ordered]@{
        'phase-a.graphemes.txt' = (New-File 'A' $graphemesA -Reviewed:$Reviewed)
        'phase-a.words.txt'     = (New-File 'A' $wordsA -Reviewed:$Reviewed)
        'phase-b.graphemes.txt' = (New-File 'B' $graphemesB -Reviewed:$Reviewed)
        'phase-b.words.txt'     = (New-File 'B' $wordsB -Reviewed:$Reviewed)
    }
}

$failures = [System.Collections.Generic.List[string]]::new()
$count = 0
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("wjs_check_phonics_" + [guid]::NewGuid().ToString('N'))

# Builds a root from the files, runs the checker and judges it.
function Test-Case([string]$Name, [int]$Expected, $Files, [string[]]$Expect = @(), [string[]]$Arguments = @()) {
    $script:count++
    $root = Join-Path $temp ([string]$script:count)
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'data/dictionaries') | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $root 'data/dictionaries/en.txt'),
        (($dictionaryWords | Sort-Object) -join "`n") + "`n")
    if ($null -ne $Files) {
        $directory = Join-Path $root 'data/phonics'
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        foreach ($fileName in $Files.Keys) {
            [System.IO.File]::WriteAllText((Join-Path $directory $fileName), $Files[$fileName],
                [System.Text.UTF8Encoding]::new($false))
        }
    }

    $output = & $pwsh -NoProfile -NonInteractive -File $checker -Root $root @Arguments 2>&1
    $code = $LASTEXITCODE
    $text = $output -join "`n"

    $missing = @($Expect | Where-Object { -not $text.Contains($_) })
    if ($code -eq $Expected -and $missing.Count -eq 0) {
        Write-Host ("  ok    {0}" -f $Name)
    } else {
        Write-Host ("  FAIL  {0}: expected exit {1}, got {2}" -f $Name, $Expected, $code) -ForegroundColor Red
        foreach ($m in $missing) { Write-Host "        missing from the output: $m" -ForegroundColor Red }
        Write-Host $text
        $script:failures.Add($Name)
    }
}

# A copy of the good phases with one file replaced.
function With-File($Phases, [string]$Name, [string]$Text) {
    $copy = [ordered]@{}
    foreach ($key in $Phases.Keys) { $copy[$key] = $Phases[$key] }
    $copy[$Name] = $Text
    return $copy
}

try {
    New-Item -ItemType Directory -Force -Path $temp | Out-Null
    $good = New-Phases
    $reviewed = New-Phases -Reviewed

    Write-Host 'No data, and good data'
    Test-Case 'no phonics directory is nothing to check' 0 $null @('nothing to check')
    Test-Case 'two phases of test data pass' 0 $good @('OK:', '4 of 4 are test data', '11 word(s)')
    Test-Case 'two reviewed phases pass' 0 $reviewed @('all reviewed')
    Test-Case 'a tricky word needs no split' 0 (With-File $good 'phase-a.words.txt' (New-File 'A' "nip  n i p`nxyz  tricky")) @('OK:')
    Test-Case 'a phase with only a words file is a phase' 0 ([ordered]@{
            'phase-a.graphemes.txt' = (New-File 'A' $graphemesA)
            'phase-a.words.txt'     = (New-File 'A' $wordsA)
            'phase-b.words.txt'     = (New-File 'B' 'xyz  tricky')
        }) @('OK:')

    Write-Host 'The review gate'
    Test-Case 'test data is refused with -RequireReviewed' 1 $good @('has no complete review header', 'phase-a.words.txt') @('-RequireReviewed')
    Test-Case 'reviewed data passes -RequireReviewed' 0 $reviewed @('all reviewed') @('-RequireReviewed')
    Test-Case 'one test file among reviewed ones is named' 1 (With-File $reviewed 'phase-b.words.txt' (New-File 'B' $wordsB)) @('phase-b.words.txt') @('-RequireReviewed')
    Test-Case 'placeholders left in the header are test data' 1 (With-File $reviewed 'phase-a.words.txt' ("# phase: A`n# reviewed-by: <name>   reviewed-on: <date>   source: <where>`n$wordsA`n")) @('phase-a.words.txt') @('-RequireReviewed')
    Test-Case 'a reviewer without a date is a problem' 1 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-by: Test Person`n$wordsA`n")) @('give both reviewed-by and reviewed-on')
    Test-Case 'a date without a reviewer is a problem' 1 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-on: 2026-10-20`n$wordsA`n")) @('give both reviewed-by and reviewed-on')
    Test-Case 'a date that is not a date is a problem' 1 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-by: Test Person   reviewed-on: last week`n$wordsA`n")) @('YYYY-MM-DD')
    Test-Case 'a day the month does not have is a problem' 1 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-by: Test Person   reviewed-on: 2026-02-29`n$wordsA`n")) @('YYYY-MM-DD')
    Test-Case 'a leap day is a date' 0 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-by: Test Person   reviewed-on: 2028-02-29   source: a test`n$wordsA`n")) @('OK:')
    Test-Case 'a reviewer and a date and no source is a problem' 1 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-by: Test Person   reviewed-on: 2026-10-20`n$wordsA`n")) @("says where its list came from")
    Test-Case 'a source still in angle brackets is no source' 1 (With-File $good 'phase-a.words.txt' ("# phase: A`n# reviewed-by: Test Person   reviewed-on: 2026-10-20   source: <where>`n$wordsA`n")) @("says where its list came from")
    Test-Case 'test data needs no source' 0 (With-File $good 'phase-a.words.txt' ("# phase: A`n# source: a list written in the test`n$wordsA`n")) @('OK:')

    Write-Host 'The header and the file names'
    Test-Case 'no phase line is a problem' 1 (With-File $good 'phase-a.words.txt' $wordsA) @("no '# phase: <name>' header")
    Test-Case 'a header that names another phase than the file' 1 (With-File $good 'phase-a.words.txt' (New-File 'B' $wordsA)) @('its header says phase B, its name says phase A')
    Test-Case 'a .txt file of another name is a problem' 1 (With-File $good 'extra.txt' 'nip') @('extra.txt', 'not named phase-<x>')

    Write-Host 'The line grammar'
    $badWords = {
        param([string]$Body)
        With-File $good 'phase-a.words.txt' (New-File 'A' $Body)
    }
    Test-Case 'a word that is not lowercase letters' 1 (& $badWords "Nip  n i p") @("phase-a.words.txt:2", "'Nip' is not a lowercase a-z word")
    Test-Case 'a word too short to be counted' 1 (& $badWords "it  i t") @('fewer than 3 letters')
    Test-Case 'a word with nothing after it' 1 (& $badWords "nip") @('has no graphemes')
    Test-Case 'a split digraph written joined is refused, with the way to write it' 1 (& $badWords "tap  t a_e p") @("'a_e'", 'halves of a split digraph')
    Test-Case 'a word whose graphemes do not spell it' 1 (& $badWords "nip  n i t") @("phase-a.words.txt:2", "the graphemes of 'nip' spell 'nit'")
    Test-Case 'tricky with a split as well' 1 (& $badWords "xyz  x tricky") @('stands alone')
    Test-Case 'a word twice' 1 (& $badWords "nip  n i p`nnip  n i p") @("'nip' is listed twice (first on line 2)")
    Test-Case 'words out of order' 1 (& $badWords "sit  s i t`nnip  n i p") @("'nip' is out of order (after 'sit')")
    Test-Case 'a sound without slashes' 1 (With-File $good 'phase-a.graphemes.txt' (New-File 'A' "a  t1  clip:t1")) @('not a sound written between slashes')
    Test-Case 'a clip without its prefix' 1 (With-File $good 'phase-a.graphemes.txt' (New-File 'A' "a  /t1/  t1")) @("'clip:<id>'")
    Test-Case 'the wrong number of graphemes fields' 1 (With-File $good 'phase-a.graphemes.txt' (New-File 'A' "a  /t1/")) @('a grapheme line is')
    Test-Case 'graphemes out of order' 1 (With-File $good 'phase-a.graphemes.txt' (New-File 'A' "n  /t3/  clip:t3`na  /t1/  clip:t1")) @("'a' is out of order (after 'n')")
    Test-Case 'a grapheme with the same sound twice' 1 (With-File $good 'phase-b.graphemes.txt' (New-File 'B' "ai  /t7/  clip:t7`nai  /t7/  clip:t8")) @("'ai' with the sound /t7/ is listed twice (first on line 2)")
    Test-Case 'two graphemes may share a clip' 0 (With-File $good 'phase-b.graphemes.txt' (New-File 'B' "ai  /t7/  clip:t7`nay  /t7/  clip:t7")) @('OK:')
    Test-Case 'comments, tabs, a byte-order mark and CRLF are tolerated' 0 (With-File $good 'phase-a.words.txt' ([string][char]0xFEFF + "# phase: A`r`n`r`n# a comment`r`nnip`tn  i`tp`r`n")) @('OK:')

    Write-Host 'The cross-file checks'
    Test-Case 'a word not in the dictionary' 1 (& $badWords "nip  n i p`nzap  z a p") @("phase-a.words.txt:3", "'zap' is not in the dictionary")
    Test-Case "an earlier phase's word using a later phase's grapheme" 1 (& $badWords "pain  p ai n") @("grapheme 'ai' of 'pain' is not in phase A or an earlier phase")
    Test-Case "a later phase's word may use an earlier phase's graphemes" 0 $good @('OK:')
    Test-Case 'a word listed in two phases' 1 (With-File $good 'phase-b.words.txt' (New-File 'B' "nip  n i p`npain  p ai n")) @("phase-b.words.txt:2", "'nip' is already listed (phase-a.words.txt:2)")
    Test-Case 'a grapheme defined with the same sound in two phases' 1 (With-File $good 'phase-b.graphemes.txt' (New-File 'B' "ai  /t7/  clip:t7`nn  /t3/  clip:t9")) @("'n' with the sound /t3/ is already defined (phase-a.graphemes.txt:4)")
    Test-Case 'a grapheme may be defined again with another sound' 0 (With-File $good 'phase-b.graphemes.txt' (New-File 'B' "ai  /t7/  clip:t7`nz  /t8/  clip:t8`nz  /t9/  clip:t9")) @('OK:')
    Test-Case 'a word that uses a grapheme defined again must say which' 1 (With-File $good 'phase-b.graphemes.txt' (New-File 'B' "ai  /t7/  clip:t7`nn  /t9/  clip:t9")) @("grapheme 'n' of 'pain' has 2 sounds in phase B: say which")
    Test-Case 'a tricky word not in the dictionary' 1 (& $badWords "nip  n i p`nzzz  tricky") @("'zzz' is not in the dictionary")

    Write-Host 'Split digraphs'
    $splitGraphemes = "a  /t1/  clip:t1`na_e  /t8/  clip:t8`nc  /t9/  clip:t9`ni  /t2/  clip:t2`nk  /t7/  clip:t7`nn  /t3/  clip:t3`np  /t4/  clip:t4`ns  /t5/  clip:t5`nt  /t6/  clip:t6"
    $withSplit = With-File (With-File $good 'phase-a.graphemes.txt' (New-File 'A' $splitGraphemes)) 'phase-a.words.txt' (New-File 'A' "cake  c a_ k _e`n$wordsA")
    Test-Case 'a split digraph in two halves passes' 0 $withSplit @('OK:', '12 word(s)')
    $splitWord = {
        param([string]$Line)
        With-File (With-File $good 'phase-a.graphemes.txt' (New-File 'A' $splitGraphemes)) 'phase-a.words.txt' (New-File 'A' "$Line`n$wordsA")
    }
    Test-Case 'a split digraph opened and not closed' 1 (& $splitWord "cake  c a_ k e") @('opened and not closed')
    Test-Case 'a split digraph closed and not opened' 1 (& $splitWord "cake  c a k _e") @('closes a split digraph that was not opened')
    Test-Case 'a split digraph with nothing between its halves' 1 (& $splitWord "cake  c a_ _e k") @('nothing between them')
    Test-Case 'a split digraph inside another' 1 (& $splitWord "cake  c a_ k i_ _e") @('inside another')
    Test-Case 'a split digraph whose halves do not spell the word' 1 (& $splitWord "cake  c a_ k _i") @("the graphemes of 'cake' spell 'caki'")
    Test-Case 'a split digraph that no phase defines' 1 (With-File $good 'phase-a.words.txt' (New-File 'A' "cake  c a_ k _e`n$wordsA")) @("grapheme 'c' of 'cake' is not in phase A", "grapheme 'a_e' of 'cake' is not in phase A")

    Write-Host 'A grapheme with more than one sound'
    $soundGraphemes = "ai  /t7/  clip:t7`nc  /t1/  clip:t1`now  /t8/  clip:t8`now  /t9/  clip:t9"
    $withSounds = With-File $good 'phase-b.graphemes.txt' (New-File 'B' $soundGraphemes)
    $soundWords = {
        param([string]$Body)
        With-File $withSounds 'phase-b.words.txt' (New-File 'B' $Body)
    }
    Test-Case 'a word that picks one of two sounds' 0 (& $soundWords "cow  c ow:2`npain  p ai n`npaint  p ai n t`nsaint  s ai n t`nsnow  s n ow:1`nstain  s t ai n") @('OK:')
    Test-Case 'a word that does not say which of two sounds' 1 (& $soundWords "cow  c ow`npain  p ai n") @("has 2 sounds in phase B: say which")
    Test-Case 'a number past the sounds there are' 1 (& $soundWords "cow  c ow:3`npain  p ai n") @("is numbered 3 and has only 2 sound(s)")
    Test-Case 'a number that is not a number' 1 (& $soundWords "cow  c ow:0`npain  p ai n") @("is not a grapheme")

    Write-Host 'The repository itself'
    $script:count++
    $output = & $pwsh -NoProfile -NonInteractive -File $checker -Root $repoRoot 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host '  ok    the repository''s own data passes'
    } else {
        Write-Host '  FAIL  the repository''s own data passes' -ForegroundColor Red
        Write-Host ($output -join "`n")
        $failures.Add('the repository''s own data passes')
    }
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host ("{0} of {1} case(s) failed." -f $failures.Count, $count) -ForegroundColor Red
    exit 1
}
Write-Host ("OK: {0} case(s) behaved." -f $count) -ForegroundColor Green
exit 0
