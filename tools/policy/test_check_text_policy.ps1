#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Self-test for check_text_policy.ps1: a text policy that silently passes
# everything is worse than none, so CI (commit-policy.yml) runs this before it
# trusts the policy with a pull request.
#
# Every sample that must FAIL is built here at run time from fragments, so this
# file contains none of the names or phrases the policy forbids (the last case
# runs the policy over this very file).
#
# Usage:  ./tools/policy/test_check_text_policy.ps1
# Exit:   0 = all cases behaved, 1 = at least one did not.

$ErrorActionPreference = 'Stop'

$policy = Join-Path $PSScriptRoot 'check_text_policy.ps1'
$pwsh = (Get-Process -Id $PID).Path
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path

function Join-Parts([string[]]$Parts) { return ($Parts -join '') }

# Fragments of things the policy forbids.
$trailerKey = Join-Parts 'Co-auth', 'ored-', 'by'
$creditPhrase = Join-Parts 'Gener', 'ated ', 'wi', 'th'
$creditPhraseBy = Join-Parts 'gener', 'ated b', 'y'
$toolOne = Join-Parts 'Cla', 'ude'
$vendorOne = Join-Parts 'Anth', 'ropic'
$toolTwo = Join-Parts 'Chat', 'G', 'PT'
$modelName = Join-Parts 'g', 'pt-4o'
$noreply = Join-Parts 'no', 'reply@', 'example.com'
$field = Join-Parts 'A', 'I'
$robot = [char]::ConvertFromUtf32(0x1F916)
$scissors = '# ------------------------ >8 ------------------------'

$failures = [System.Collections.Generic.List[string]]::new()
$count = 0

function Invoke-Policy([string[]]$Arguments, [string]$StdinText) {
    if ($PSBoundParameters.ContainsKey('StdinText')) {
        $output = $StdinText | & $pwsh -NoProfile -NonInteractive -File $policy @Arguments 2>&1
    } else {
        $output = & $pwsh -NoProfile -NonInteractive -File $policy @Arguments 2>&1
    }
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output -join "`n") }
}

function Test-Case([string]$Name, [int]$Expected, [string[]]$Arguments, [string]$StdinText) {
    $script:count++
    $result = if ($PSBoundParameters.ContainsKey('StdinText')) {
        Invoke-Policy $Arguments $StdinText
    } else {
        Invoke-Policy $Arguments
    }
    if ($result.ExitCode -eq $Expected) {
        Write-Host ("  ok    {0}" -f $Name)
    } else {
        Write-Host ("  FAIL  {0}: expected exit {1}, got {2}" -f $Name, $Expected, $result.ExitCode) -ForegroundColor Red
        if ($result.Output) { Write-Host $result.Output }
        $script:failures.Add($Name)
    }
}

Write-Host 'check_text_policy.ps1 self-test'

# Text that must pass.
Test-Case 'clean one-line message' 0 @('-Text', 'Add the dictionary loader')
Test-Case 'empty text' 0 @('-Text', '')
Test-Case 'clean multi-line message on stdin' 0 @() "Fix board parsing`n`nA row with a trailing space was rejected; trim it first.`nCloses #12."
Test-Case 'a person''s own GitHub noreply address' 0 @('-Text', 'Reported by 12345+someone@users.noreply.github.com')
Test-Case 'ordinary words that merely contain a forbidden fragment' 0 @('-Text', 'Cursor position, a pilot run and the codec table')

# Text that must fail.
Test-Case 'co-author trailer' 1 @('-Text', "${trailerKey}: Someone <someone@example.com>")
Test-Case 'co-author trailer, odd case' 1 @('-Text', "$($trailerKey.ToUpperInvariant()): Someone")
Test-Case 'credit line' 1 @('-Text', "$creditPhrase a tool")
Test-Case 'credit line, "by" form' 1 @('-Text', "This was $creditPhraseBy a script")
Test-Case 'tool name' 1 @('-Text', "Thanks to $toolOne for the idea")
Test-Case 'vendor name' 1 @('-Text', "an idea from $vendorOne")
Test-Case 'second tool name' 1 @('-Text', "pasted from $toolTwo")
Test-Case 'model name' 1 @('-Text', "output of $modelName")
Test-Case 'noreply address' 1 @('-Text', "Signed $noreply")
Test-Case 'tool-authorship adjective' 1 @('-Text', "$field-generated tests")
Test-Case 'robot emoji' 1 @('-Text', "$robot done")
Test-Case 'violation deep in a multi-line body on stdin' 1 @() "Subject`n`nBody line one.`nBody line two.`n${trailerKey}: Someone"
Test-Case 'violation in a heading line (markdown "#")' 1 @() "## $creditPhrase a tool"

# Git commit-message mode: comments and the verbose diff are not message text.
Test-Case 'comment line is ignored in -GitMessage mode' 0 @('-GitMessage') "Subject`n`n# $creditPhrase a tool`n"
Test-Case 'the same line fails in plain mode' 1 @() "Subject`n`n# $creditPhrase a tool`n"
Test-Case 'diff below the scissors line is ignored' 0 @('-GitMessage') "Subject`n$scissors`n+ $toolOne`n"
Test-Case 'text above the scissors line still counts' 1 @('-GitMessage') "Subject $toolOne`n$scissors`n+ diff`n"

# Files, and bad invocations.
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("wjs-policy-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp | Out-Null
try {
    $cleanFile = Join-Path $temp 'clean.txt'
    $badFile = Join-Path $temp 'bad.txt'
    Set-Content -LiteralPath $cleanFile -Value "Add a thing`n`nIt works." -NoNewline
    Set-Content -LiteralPath $badFile -Value "Add a thing`n`n${trailerKey}: Someone" -NoNewline
    Test-Case '-Path with a clean file' 0 @('-Path', $cleanFile)
    Test-Case '-Path with a file that breaks the rule' 1 @('-Path', $badFile)
    Test-Case '-Path with a missing file' 2 @('-Path', (Join-Path $temp 'missing.txt'))
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}

# The files this repository ships that end up as PR text, or that state the
# rule, pass it themselves.
Test-Case 'the policy script passes its own check' 0 @('-Path', $policy)
Test-Case 'this test passes the policy' 0 @('-Path', $PSCommandPath)
$template = Join-Path $repoRoot '.github/PULL_REQUEST_TEMPLATE.md'
if (Test-Path -LiteralPath $template) {
    Test-Case 'the pull request template passes the policy' 0 @('-Path', $template)
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host ("{0} of {1} case(s) failed: {2}" -f $failures.Count, $count, ($failures -join '; ')) -ForegroundColor Red
    exit 1
}
Write-Host ("OK: {0} case(s) behaved as expected." -f $count) -ForegroundColor Green
exit 0
