#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Makes the project's own dice for the standard board and writes them to
# data/dice/<n>x<n>.txt.
#
# Why: the faces of a printed set may belong to someone, and a list found online
# has no origin (docs/design/board.md, "Dice"). These dice are the project's own
# work. Their letter weights are the published English tile counts of Scrabble,
# a public-domain-style frequency table, not the faces of any printed die set.
# The dictionary is not used: its letter counts favour the letters of long
# dictionary words rather than the letters of English, and they put too few
# vowels and too many S on a small board.
#
# Method, which is also written into the header of each file:
#
#   - The weights are the 100-tile English counts, the two blanks not counted
#     (98 letter tiles): A9 B2 C2 D4 E12 F2 G3 H2 I9 J1 K1 L4 M2 N6 O8 P2 Q1
#     R6 S4 T6 U4 V2 W2 X1 Y2 Z1.
#   - A die has six different letters, drawn without replacement in proportion
#     to those weights. A die is dealt again unless it has one to three vowels
#     (A E I O U) and at most one of J K Q V X Z, so no die is a heap of rare
#     letters.
#   - A set is dealt again unless vowel faces are 30% to 40% of its faces and S
#     faces are at most 8% of them, so a small board has vowels to make words
#     with and is not a board of plurals.
#   - A Q is the two-letter tile QU, as on a Boggle die.
#   - A size n by n has n * n dice. Each size has its own seed and its own dice:
#     the 3x3 is not a window on the 4x4.
#   - The draw is a 32-bit linear congruential generator, so the files can be
#     made again on any machine: state = state * 1664525 + 1013904223 mod 2^32,
#     and a draw below n is (state >> 8) mod n. A die or a set that breaks a
#     rule is dealt again from the next draw, never from a fresh seed.
#
# The header records the weights, the method and the seed, so a reader can see
# where every letter came from. No dictionary hash is recorded because the
# dictionary is no longer an input.
#
# Usage:  ./tools/data/make_dice.ps1 [-Root <repo>] [-Size 3,4,5,6]   write
#         ./tools/data/make_dice.ps1 -Check                           compare, write nothing
# Exit:   0 = written or matching, 1 = a file differs or is missing, 2 = bad invocation.

param(
    [string]$Root = (Join-Path $PSScriptRoot '../..'),
    [int[]]$Size = @(3, 4, 5, 6),
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$outputDirectory = Join-Path $Root 'data/dice'

$facesPerDie = 6
$vowels = 'A', 'E', 'I', 'O', 'U'
$rare = 'J', 'K', 'Q', 'V', 'X', 'Z'

# The seed of each standard set. The small sets' seeds are chosen from the
# generator's own stream so that the 3x3 and 4x4 keep C and H and a useful
# spread of common letters as well as passing the quality rules below. A size
# without a seed here uses 7919 * side.
$seeds = @{ 3 = 86870; 4 = 78271; 5 = 39595; 6 = 47514 }

# The English Scrabble tile counts: 98 letter tiles, the two blanks not faces.
$table = 'A9 B2 C2 D4 E12 F2 G3 H2 I9 J1 K1 L4 M2 N6 O8 P2 Q1 R6 S4 T6 U4 V2 W2 X1 Y2 Z1'
$letters = [char[]](65..90)
$weights = [int[]](9, 2, 2, 4, 12, 2, 3, 2, 9, 1, 1, 4, 2, 6, 8, 2, 1, 6, 4, 6, 4, 2, 2, 1, 2, 1)

$minVowelPercent = 30
$maxVowelPercent = 40
$maxSPercent = 8
$maxVowelsPerDie = 3
$maxRarePerDie = 1

$isVowel = [bool[]]::new(26)
foreach ($letter in $vowels) {
    $isVowel[[int][char]$letter - 65] = $true
}
$isRare = [bool[]]::new(26)
foreach ($letter in $rare) {
    $isRare[[int][char]$letter - 65] = $true
}

# --- the generator ------------------------------------------------------------

$script:state = [uint64]0

function Set-Seed([int]$seed) { $script:state = [uint64]$seed -band 0xFFFFFFFF }

# A draw below $n: step the generator, take its high bits.
function Get-Draw([int]$n) {
    $script:state = ($script:state * 1664525 + 1013904223) % 4294967296
    return [int](($script:state -shr 8) % $n)
}

# One die: six different letters dealt in proportion to the table, with one to
# three vowels and no more than one rare letter.
function New-Die {
    while ($true) {
        $remaining = [int[]]$weights.Clone()
        $faces = [System.Collections.Generic.List[int]]::new()
        $vowelFaces = 0
        $rareFaces = 0
        for ($face = 0; $face -lt $facesPerDie; $face++) {
            $total = 0
            foreach ($weight in $remaining) { $total += $weight }
            $pick = Get-Draw $total
            $running = 0
            for ($i = 0; $i -lt 26; $i++) {
                $running += $remaining[$i]
                if ($running -gt $pick) {
                    $remaining[$i] = 0
                    $faces.Add($i)
                    if ($isVowel[$i]) { $vowelFaces++ }
                    if ($isRare[$i]) { $rareFaces++ }
                    break
                }
            }
        }
        if ($vowelFaces -ge 1 -and $vowelFaces -le $maxVowelsPerDie -and
            $rareFaces -le $maxRarePerDie) {
            return ,$faces.ToArray()
        }
    }
}

# A die's faces, upper-case, sorted in ordinal letter order, Q as QU.
function Format-Die([int[]]$faces) {
    $tiles = [string[]]::new($faces.Count)
    for ($i = 0; $i -lt $faces.Count; $i++) {
        $tiles[$i] = if ($letters[$faces[$i]] -eq 'Q') { 'QU' } else { [string]$letters[$faces[$i]] }
    }
    [System.Array]::Sort($tiles, [System.StringComparer]::Ordinal)
    return ($tiles -join ' ')
}

# The quality rules, as a list of problems empty when the set is good. This is
# the deterministic check that -Check and the writer both use: six different
# sorted faces to a die, one to three vowels on each, a sensible vowel share,
# a cap on S, and Q only as QU.
function Get-DiceProblems([string[]]$Lines, [int]$side) {
    $problems = [System.Collections.Generic.List[string]]::new()
    $expectedDice = $side * $side
    if ($Lines.Count -ne $expectedDice) {
        $problems.Add("has $($Lines.Count) dice, not $expectedDice")
        return ,$problems
    }

    $vowelFaces = 0
    $sFaces = 0
    for ($die = 0; $die -lt $Lines.Count; $die++) {
        $faces = [string[]]@($Lines[$die].Trim() -split '\s+' | Where-Object { $_ -ne '' })
        if ($faces.Count -ne $facesPerDie) {
            $problems.Add("die $($die + 1) has $($faces.Count) faces, not $facesPerDie")
            continue
        }

        $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
        $sorted = [string[]]$faces.Clone()
        [System.Array]::Sort($sorted, [System.StringComparer]::Ordinal)
        if (($faces -join ' ') -cne ($sorted -join ' ')) {
            $problems.Add("die $($die + 1) is not in letter order")
        }

        $dieVowels = 0
        $dieRare = 0
        foreach ($face in $faces) {
            if ($face -notmatch '^[A-Z]$|^QU$') {
                $problems.Add("die $($die + 1) has the face '$face', not a letter or QU")
                continue
            }
            if (-not $seen.Add($face)) {
                $problems.Add("die $($die + 1) repeats $face")
            }
            if ($face -eq 'QU') {
                # QU is the Q tile; the U belongs to it and is not counted as a
                # separate vowel face for the density rule.
                $dieRare++
            } elseif ($face -eq 'Q') {
                $problems.Add("die $($die + 1) has a bare Q; Q is the tile QU")
            } else {
                if ($vowels -contains $face) { $dieVowels++ }
                if ($rare -contains $face) { $dieRare++ }
            }
        }
        if ($dieVowels -lt 1) {
            $problems.Add("die $($die + 1) has no vowel")
        }
        if ($dieVowels -gt $maxVowelsPerDie) {
            $problems.Add("die $($die + 1) has $dieVowels vowels, more than $maxVowelsPerDie")
        }
        if ($dieRare -gt $maxRarePerDie) {
            $problems.Add("die $($die + 1) has $dieRare rare letters, more than $maxRarePerDie")
        }
        $vowelFaces += $dieVowels
        foreach ($face in $faces) {
            if ($face -eq 'S') { $sFaces++ }
        }
    }

    $facesTotal = $expectedDice * $facesPerDie
    if ($vowelFaces * 100 -lt $facesTotal * $minVowelPercent) {
        $problems.Add("has $vowelFaces vowel faces of $facesTotal, under $minVowelPercent%")
    }
    if ($vowelFaces * 100 -gt $facesTotal * $maxVowelPercent) {
        $problems.Add("has $vowelFaces vowel faces of $facesTotal, over $maxVowelPercent%")
    }
    if ($sFaces * 100 -gt $facesTotal * $maxSPercent) {
        $problems.Add("has $sFaces S faces of $facesTotal, over $maxSPercent%")
    }
    return ,$problems
}

# A whole set: dice dealt one by one, and the set dealt again until the quality
# rules hold. The seed stays the one in the header; only the draw moves on.
function New-DiceSet([int]$side) {
    $seed = if ($seeds.ContainsKey($side)) { $seeds[$side] } else { 7919 * $side }
    Set-Seed $seed
    while ($true) {
        $lines = [System.Collections.Generic.List[string]]::new()
        for ($die = 0; $die -lt $side * $side; $die++) {
            $lines.Add((Format-Die (New-Die)))
        }
        if ((Get-DiceProblems -Lines $lines.ToArray() -side $side).Count -eq 0) {
            $vowelFaces = 0
            $sFaces = 0
            foreach ($line in $lines) {
                foreach ($face in $line -split ' ') {
                    if ($vowels -contains $face) { $vowelFaces++ }
                    if ($face -eq 'S') { $sFaces++ }
                }
            }
            return [pscustomobject]@{
                seed = $seed
                lines = $lines.ToArray()
                vowels = $vowelFaces
                s = $sFaces
            }
        }
    }
}

function New-DiceFile([int]$side) {
    $set = New-DiceSet $side
    $header = @(
        "# dice for a ${side}x${side} board: $($side * $side) dice of $facesPerDie faces, one a line"
        '# made-by: tools/data/make_dice.ps1'
        '# table: Scrabble-style English tile frequencies, the 100-tile English counts'
        '#   (98 letter tiles; the two blanks are not faces). These are published English'
        '#   letter counts, not a copied printed die set:'
        "#   $table"
        '# method: the project''s own dice. Six different letters to a die, drawn without'
        '#   replacement in proportion to the table; a die is dealt again unless it has one'
        "#   to three vowels and at most one of J K Q V X Z. A set is dealt again unless"
        "#   $minVowelPercent% to $maxVowelPercent% of its faces are vowels and S is at most $maxSPercent% of them."
        '#   Q is the tile QU. Faces of a die are in ordinal letter order. Each size has'
        '#   its own seed and its own dice: the 3x3 is not a window on the 4x4.'
        '# prng: 32-bit LCG, state = state * 1664525 + 1013904223 mod 2^32; a draw below n is (state >> 8) mod n'
        "# seed: $($set.seed)"
    )
    return [pscustomobject]@{
        seed = $set.seed
        lines = $set.lines
        vowels = $set.vowels
        s = $set.s
        text = ((($header + $set.lines) -join "`n") + "`n")
    }
}

# --- write, or compare --------------------------------------------------------

$problems = [System.Collections.Generic.List[string]]::new()
foreach ($side in $Size) {
    if ($side -lt 1 -or $side -gt 16) {
        Write-Error "A size is 1 to 16, not $side."
        exit 2
    }
    $name = "${side}x${side}.txt"
    $path = Join-Path $outputDirectory $name
    $wanted = New-DiceFile $side

    if ($Check) {
        if (-not (Test-Path -LiteralPath $path)) {
            $problems.Add("data/dice/${name}: missing; run tools/data/make_dice.ps1")
            continue
        }
        $actual = [System.IO.File]::ReadAllText($path).Replace("`r`n", "`n")
        if ($actual -cne $wanted.text) {
            $problems.Add("data/dice/${name}: is not what tools/data/make_dice.ps1 makes; run the tool again")
            continue
        }
        foreach ($problem in (Get-DiceProblems -Lines $wanted.lines -side $side)) {
            $problems.Add("data/dice/${name}: $problem")
        }
    } else {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
        [System.IO.File]::WriteAllText($path, $wanted.text, [System.Text.UTF8Encoding]::new($false))
        Write-Host "wrote data/dice/$name ($($side * $side) dice, $($wanted.vowels) vowel faces, $($wanted.s) S faces)"
    }
}

if ($problems.Count -gt 0) {
    Write-Host ''
    Write-Host 'Dice problem(s):' -ForegroundColor Red
    foreach ($problem in $problems) {
        Write-Host "  $problem"
        $file = $problem.Substring(0, $problem.IndexOf(':'))
        Write-Host "::error file=${file}::$($problem.Substring($problem.IndexOf(':') + 1).Trim())"
    }
    exit 1
}

if ($Check) {
    Write-Host "OK: $($Size.Count) dice file(s) are what tools/data/make_dice.ps1 makes from the Scrabble-style table." -ForegroundColor Green
}
exit 0
