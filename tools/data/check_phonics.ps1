#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Holds the phonics phase files under data/phonics to the rules the
# application relies on (docs/testing.md, "Phonics data"). It reads the same
# format as libs/core/src/phase_data.cpp and applies the same checks, so a file
# that passes here loads there.
#
#   data/phonics/phase-<x>.graphemes.txt   one grapheme and a sound a line:
#                                            ai  /sound/  clip:ai
#   data/phonics/phase-<x>.words.txt       one word a line:
#                                            pain  p ai n
#                                            <word>  tricky
#
# A split digraph is written as its two halves around the letters between,
# `cake  c a_ k _e`, and is the grapheme `a_e` in the graphemes file. A
# grapheme that makes more than one sound is listed once for each sound, and a
# word line says which it uses, counting from 1 in the order of that grapheme's
# lines: `snow  s n ow:2`. A split digraph's number goes on its first half,
# `a_:2`.
#
# Each file starts with `# phase: <x>` and may carry
# `# reviewed-by: <name>   reviewed-on: <date>   source: <where it came from>`.
# A file without all three (and reviewed-on as YYYY-MM-DD) is test data.
# -RequireReviewed makes that a failure, which is the release build's gate
# (WJS_REQUIRE_REVIEWED_PHONICS).
#
# Checks, each reported with file and line:
#   - the file name is phase-<x>.graphemes.txt or phase-<x>.words.txt and the
#     header names the same phase;
#   - the line grammar; lowercase a-z words of three or more letters, graphemes
#     of lowercase letters, sounds between slashes, clip ids;
#   - the graphemes of a word, joined, spell the word (not for `tricky`);
#   - no word twice, and no grapheme with the same sound twice, in a file or
#     across phases; sorted (byte order);
#   - every word is in data/dictionaries/en.txt;
#   - every grapheme of a word is in the word's phase or an earlier one
#     (phases are cumulative, ordered by name), and a grapheme with several
#     sounds there is told which.
#
# No data/phonics directory, or none with a .txt file in it, is not a problem:
# there is nothing to check.
#
# Exit: 0 = clean, 1 = violation(s), 2 = bad invocation.

param(
    [string]$Root = (Join-Path $PSScriptRoot '../..'),
    [switch]$RequireReviewed
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$phonicsDir = Join-Path $Root 'data/phonics'
$english = Join-Path $Root 'data/dictionaries/en.txt'

$minWordLetters = 3

# --- what there is -----------------------------------------------------------

$files = @()
if (Test-Path -LiteralPath $phonicsDir) {
    $files = @(Get-ChildItem -LiteralPath $phonicsDir -Filter *.txt -File | Sort-Object Name)
}
if ($files.Count -eq 0) {
    Write-Host 'OK: no phonics data under data/phonics, so nothing to check.' -ForegroundColor Green
    exit 0
}
if (-not (Test-Path -LiteralPath $english)) {
    Write-Error "No data/dictionaries/en.txt under '$Root'."
    exit 2
}

$problems = [System.Collections.Generic.List[object]]::new()
function Add-Problem([string]$file, [int]$line, [string]$message) {
    $script:problems.Add([pscustomobject]@{ File = $file; Line = $line; Message = $message })
}

function Test-IsoDate([string]$text) {
    $parsed = [datetime]::MinValue
    return $text -cmatch '^[0-9]{4}-[0-9]{2}-[0-9]{2}$' -and [datetime]::TryParseExact(
        $text, 'yyyy-MM-dd', [System.Globalization.CultureInfo]::InvariantCulture,
        [System.Globalization.DateTimeStyles]::None, [ref]$parsed)
}

# The lines of a file, without the byte-order mark and the carriage returns of
# a Windows editor.
function Read-DataLines([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $text = [System.Text.UTF8Encoding]::new($false).GetString($bytes)
    if ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) { $text = $text.Substring(1) }
    return @($text -split "`n" | ForEach-Object { $_.TrimEnd("`r") })
}

# One token of a word line after the word, as a piece: its kind (whole, open or
# close), its letters without the underscore or the number, and the sound it
# picks (counting from 0, or -1 for none). $null if it is none of those.
function Read-Piece([string]$token) {
    $sound = -1
    $colon = $token.IndexOf(':')
    if ($colon -ge 0) {
        $number = $token.Substring($colon + 1)
        if ($number -cnotmatch '^[1-9][0-9]?$') { return $null }
        $sound = [int]$number - 1
        $token = $token.Substring(0, $colon)
    }
    if ($token.StartsWith('_')) {
        # The second half picks nothing: the first half does.
        if ($colon -ge 0 -or $token.Substring(1) -cnotmatch '^[a-z]+$') { return $null }
        return [pscustomobject]@{ Kind = 'close'; Letters = $token.Substring(1); Sound = $sound }
    }
    if ($token.EndsWith('_')) {
        $letters = $token.Substring(0, $token.Length - 1)
        if ($letters -cnotmatch '^[a-z]+$') { return $null }
        return [pscustomobject]@{ Kind = 'open'; Letters = $letters; Sound = $sound }
    }
    if ($token -cnotmatch '^[a-z]+$') { return $null }
    return [pscustomobject]@{ Kind = 'whole'; Letters = $token; Sound = $sound }
}

# A file read: its name, phase, kind, header and entries. Entries are objects
# with Line and, for words, Word, Graphemes (each Letters and Sound) and Tricky;
# for graphemes, Letters, Sound and Clip.
function Read-PhaseFile([System.IO.FileInfo]$file) {
    $name = $file.Name
    $result = [pscustomobject]@{
        Name = $name; Phase = ''; Kind = ''; Reviewed = $false; Entries = @(); Ok = $true
    }

    $match = [regex]::Match($name, '^phase-([A-Za-z0-9]+)\.(graphemes|words)\.txt$')
    if (-not $match.Success) {
        Add-Problem $name 0 'not named phase-<x>.graphemes.txt or phase-<x>.words.txt'
        $result.Ok = $false
        return $result
    }
    $nameCase = $match.Groups[1].Value.ToUpperInvariant()
    $result.Kind = $match.Groups[2].Value

    $header = @{ phase = ''; 'reviewed-by' = ''; 'reviewed-on' = ''; source = '' }
    $seen = @{}
    $entries = [System.Collections.Generic.List[object]]::new()
    $firstLine = @{}
    $previous = ''
    $lineNumber = 0

    foreach ($raw in (Read-DataLines $file.FullName)) {
        $lineNumber++
        $text = $raw.Trim(" `t")
        if ($text.Length -eq 0) { continue }

        if ($text.StartsWith('#')) {
            $comment = $text.Substring(1)
            $hits = [regex]::Matches($comment, '(?<=^|[ \t])(phase|reviewed-by|reviewed-on|source):')
            for ($i = 0; $i -lt $hits.Count; $i++) {
                $key = $hits[$i].Groups[1].Value
                $start = $hits[$i].Index + $hits[$i].Length
                $end = if ($i + 1 -lt $hits.Count) { $hits[$i + 1].Index } else { $comment.Length }
                $value = $comment.Substring($start, $end - $start).Trim(" `t")
                if ($seen.ContainsKey($key)) {
                    Add-Problem $name $lineNumber "'${key}:' is given twice"
                    continue
                }
                $seen[$key] = $true
                if ($value -cmatch '^<.*>$') { $value = '' }
                if ($key -eq 'phase') {
                    if ($value -cmatch '^[A-Za-z0-9]+$') {
                        $header['phase'] = $value.ToUpperInvariant()
                    } else {
                        Add-Problem $name $lineNumber "the phase must be letters and digits, as in '# phase: A'"
                        $result.Ok = $false
                    }
                } else {
                    $header[$key] = $value
                }
            }
            continue
        }

        $fields = @($text -split '[ \t]+' | Where-Object { $_ })

        if ($result.Kind -eq 'graphemes') {
            if ($fields.Count -ne 3) {
                Add-Problem $name $lineNumber "a grapheme line is the letters, the sound and the clip: 'ai  /sound/  clip:ai'"
                $result.Ok = $false
                continue
            }
            $letters, $sound, $clip = $fields
            $good = $true
            if ($letters -cnotmatch '^[a-z]+(_[a-z]+)?$') {
                Add-Problem $name $lineNumber "'$letters' is not lowercase a-z letters (or two runs of them with an underscore between, for a split digraph)"
                $good = $false
            }
            if ($sound -cnotmatch '^/[^/]+/$') {
                Add-Problem $name $lineNumber "'$sound' is not a sound written between slashes"
                $good = $false
            }
            if ($clip -cnotmatch '^clip:[a-z0-9._-]+$') {
                Add-Problem $name $lineNumber "'$clip' is not a clip id written 'clip:<id>'"
                $good = $false
            }
            if (-not $good) { $result.Ok = $false; continue }
            $pair = "$letters $sound"
            if ($firstLine.ContainsKey($pair)) {
                Add-Problem $name $lineNumber "'$letters' with the sound $sound is listed twice (first on line $($firstLine[$pair]))"
                $result.Ok = $false
                continue
            }
            if ($previous -ne '' -and [string]::CompareOrdinal($previous, $letters) -gt 0) {
                Add-Problem $name $lineNumber "'$letters' is out of order (after '$previous')"
                $result.Ok = $false
            }
            $previous = $letters
            $firstLine[$pair] = $lineNumber
            $entries.Add([pscustomobject]@{ Line = $lineNumber; Letters = $letters; Sound = $sound; Clip = $clip })
            continue
        }

        # A words file.
        $word = $fields[0]
        if ($word -cnotmatch '^[a-z]+$') {
            Add-Problem $name $lineNumber "'$word' is not a lowercase a-z word"
            $result.Ok = $false
            continue
        }
        if ($word.Length -lt $minWordLetters) {
            Add-Problem $name $lineNumber "'$word' has fewer than $minWordLetters letters, so no board can count it"
            $result.Ok = $false
            continue
        }
        if ($fields.Count -eq 1) {
            Add-Problem $name $lineNumber "'$word' has no graphemes: write them after the word, or mark the word 'tricky'"
            $result.Ok = $false
            continue
        }
        $tricky = $false
        $graphemes = [System.Collections.Generic.List[object]]::new()
        if ($fields.Count -eq 2 -and $fields[1] -ceq 'tricky') {
            $tricky = $true
        } else {
            # The pieces after the word: each is a grapheme or one half of a
            # split digraph. Their letters, in the order written, have to spell
            # the word.
            $spelt = ''
            $open = -1        # index in $graphemes of a split digraph not yet closed
            $between = $false # a grapheme has come since it opened
            $good = $true
            foreach ($token in $fields[1..($fields.Count - 1)]) {
                if ($token -ceq 'tricky') {
                    Add-Problem $name $lineNumber "'$word': 'tricky' stands alone, with no split"
                    $good = $false
                    break
                }
                $piece = Read-Piece $token
                if ($null -eq $piece) {
                    Add-Problem $name $lineNumber "'$token' in '$word' is not a grapheme: letters, with ':2' to pick a second sound, or the halves of a split digraph as in 'a_' ... '_e'"
                    $good = $false
                    break
                }
                if ($piece.Kind -eq 'close') {
                    if ($open -lt 0) {
                        Add-Problem $name $lineNumber "'$token' in '$word' closes a split digraph that was not opened"
                        $good = $false
                        break
                    }
                    if (-not $between) {
                        Add-Problem $name $lineNumber "the halves of a split digraph in '$word' have nothing between them"
                        $good = $false
                        break
                    }
                    $graphemes[$open].Letters += "_$($piece.Letters)"
                    $open = -1
                } else {
                    if ($open -ge 0 -and $piece.Kind -eq 'open') {
                        Add-Problem $name $lineNumber "'$token' in '$word' opens a split digraph inside another"
                        $good = $false
                        break
                    }
                    $graphemes.Add([pscustomobject]@{ Letters = $piece.Letters; Sound = $piece.Sound })
                    if ($piece.Kind -eq 'open') {
                        $open = $graphemes.Count - 1
                        $between = $false
                    } elseif ($open -ge 0) {
                        $between = $true
                    }
                }
                $spelt += $piece.Letters
            }
            if ($good -and $open -ge 0) {
                Add-Problem $name $lineNumber "a split digraph in '$word' is opened and not closed"
                $good = $false
            }
            if ($good -and $spelt -cne $word) {
                Add-Problem $name $lineNumber "the graphemes of '$word' spell '$spelt'"
                $good = $false
            }
            if (-not $good) { $result.Ok = $false; continue }
        }
        if ($firstLine.ContainsKey($word)) {
            Add-Problem $name $lineNumber "'$word' is listed twice (first on line $($firstLine[$word]))"
            $result.Ok = $false
            continue
        }
        if ($previous -ne '' -and [string]::CompareOrdinal($previous, $word) -gt 0) {
            Add-Problem $name $lineNumber "'$word' is out of order (after '$previous')"
            $result.Ok = $false
        }
        $previous = $word
        $firstLine[$word] = $lineNumber
        $entries.Add([pscustomobject]@{ Line = $lineNumber; Word = $word; Graphemes = $graphemes.ToArray(); Tricky = $tricky })
    }

    # What the header as a whole says.
    if (-not $seen.ContainsKey('phase')) {
        Add-Problem $name 0 "no '# phase: <name>' header"
        $result.Ok = $false
    } elseif ($header['phase'] -ne '' -and $header['phase'] -cne $nameCase) {
        Add-Problem $name 0 "its header says phase $($header['phase']), its name says phase $nameCase"
        $result.Ok = $false
    }
    $by = $header['reviewed-by'] -ne ''
    $on = $header['reviewed-on'] -ne ''
    if ($by -ne $on) {
        Add-Problem $name 0 'give both reviewed-by and reviewed-on, or neither (neither is test data)'
        $result.Ok = $false
    }
    if ($on -and -not (Test-IsoDate $header['reviewed-on'])) {
        Add-Problem $name 0 'reviewed-on must be a date written YYYY-MM-DD'
        $result.Ok = $false
    }
    # A reviewed list says where it came from. Test data need not.
    $sourced = $header['source'] -ne ''
    if ($by -and $on -and -not $sourced) {
        Add-Problem $name 0 "a reviewed file says where its list came from: add 'source: <where>'"
        $result.Ok = $false
    }

    $result.Phase = $header['phase']
    $result.Reviewed = $by -and $on -and $sourced -and (Test-IsoDate $header['reviewed-on'])
    $result.Entries = $entries.ToArray()
    return $result
}

# --- read every file ---------------------------------------------------------

$parsed = @($files | ForEach-Object { Read-PhaseFile $_ })

# --- the cross-file checks ---------------------------------------------------

[string[]]$englishWords = [System.IO.File]::ReadAllLines($english)
$englishSet = [System.Collections.Generic.HashSet[string]]::new($englishWords, [System.StringComparer]::Ordinal)

$usable = @($parsed | Where-Object { $_.Ok -and $_.Phase -ne '' })

$names = [System.Collections.Generic.SortedSet[string]]::new([System.StringComparer]::Ordinal)
$graphemeFile = @{}
$wordFile = @{}
foreach ($file in $usable) {
    [void]$names.Add($file.Phase)
    $table = if ($file.Kind -eq 'graphemes') { $graphemeFile } else { $wordFile }
    if ($table.ContainsKey($file.Phase)) {
        Add-Problem $file.Name 0 "phase $($file.Phase) has a second $($file.Kind) file (the first is $($table[$file.Phase].Name))"
    } else {
        $table[$file.Phase] = $file
    }
}

# Letters -> the list of its sounds (Sound and Where), in the order defined.
$knownGraphemes = @{}
$knownWords = @{}
$graphemeCount = 0
$wordCount = 0
foreach ($phase in $names) {
    if ($graphemeFile.ContainsKey($phase)) {
        $file = $graphemeFile[$phase]
        foreach ($entry in $file.Entries) {
            if (-not $knownGraphemes.ContainsKey($entry.Letters)) {
                $knownGraphemes[$entry.Letters] = [System.Collections.Generic.List[object]]::new()
            }
            $sounds = $knownGraphemes[$entry.Letters]
            $same = $sounds | Where-Object { $_.Sound -ceq $entry.Sound } | Select-Object -First 1
            if ($null -ne $same) {
                Add-Problem $file.Name $entry.Line "'$($entry.Letters)' with the sound $($entry.Sound) is already defined ($($same.At))"
            } else {
                $sounds.Add([pscustomobject]@{ Sound = $entry.Sound; At = "$($file.Name):$($entry.Line)" })
                $graphemeCount++
            }
        }
    }
    if ($wordFile.ContainsKey($phase)) {
        $file = $wordFile[$phase]
        foreach ($entry in $file.Entries) {
            if (-not $englishSet.Contains($entry.Word)) {
                Add-Problem $file.Name $entry.Line "'$($entry.Word)' is not in the dictionary"
            }
            foreach ($grapheme in $entry.Graphemes) {
                if (-not $knownGraphemes.ContainsKey($grapheme.Letters)) {
                    Add-Problem $file.Name $entry.Line "grapheme '$($grapheme.Letters)' of '$($entry.Word)' is not in phase $phase or an earlier phase"
                    continue
                }
                # Which sound, where there is a choice.
                $count = $knownGraphemes[$grapheme.Letters].Count
                if ($grapheme.Sound -lt 0) {
                    if ($count -gt 1) {
                        Add-Problem $file.Name $entry.Line "grapheme '$($grapheme.Letters)' of '$($entry.Word)' has $count sounds in phase ${phase}: say which, as '$($grapheme.Letters):1' to '$($grapheme.Letters):$count'"
                    }
                } elseif ($grapheme.Sound -ge $count) {
                    Add-Problem $file.Name $entry.Line "grapheme '$($grapheme.Letters)' of '$($entry.Word)' is numbered $($grapheme.Sound + 1) and has only $count sound(s) in phase $phase"
                }
            }
            if ($knownWords.ContainsKey($entry.Word)) {
                Add-Problem $file.Name $entry.Line "'$($entry.Word)' is already listed ($($knownWords[$entry.Word]))"
            } else {
                $knownWords[$entry.Word] = "$($file.Name):$($entry.Line)"
                $wordCount++
            }
        }
    }
}

# --- the review gate ---------------------------------------------------------

$unreviewed = @($parsed | Where-Object { -not $_.Reviewed } | ForEach-Object { $_.Name })
if ($RequireReviewed) {
    foreach ($name in $unreviewed) {
        Add-Problem $name 0 'has no complete review header (reviewed-by, reviewed-on and source), so it is test data, which a release build refuses'
    }
}

# --- report ------------------------------------------------------------------

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host 'Phonics data problem(s):' -ForegroundColor Red
    foreach ($problem in $problems) {
        $where = if ($problem.Line -gt 0) { "$($problem.File):$($problem.Line)" } else { $problem.File }
        Write-Host "  ${where}  $($problem.Message)"
        $annotation = "file=data/phonics/$($problem.File)"
        if ($problem.Line -gt 0) { $annotation += ",line=$($problem.Line)" }
        Write-Host "::error ${annotation}::$($problem.Message)"
    }
    Write-Host ''
    Write-Host ("{0} problem(s) in {1} file(s)." -f $problems.Count, $files.Count) -ForegroundColor Red
    exit 1
}

$state = if ($unreviewed.Count -eq 0) { 'all reviewed' } else { "$($unreviewed.Count) of $($files.Count) are test data (no complete review header)" }
Write-Host "OK: $($files.Count) phonics file(s), $wordCount word(s), $graphemeCount grapheme(s); $state." -ForegroundColor Green
exit 0
