#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
#
# Text policy for commit messages, pull request titles and pull request bodies
# (docs/ci.md, "Commit and PR text policy").
#
# The rule: text that lands in this repository's history or on its pull
# requests carries NO attribution of any kind -
#
#   * no co-author trailers (or any mention of one);
#   * no lines crediting a tool with having generated the text or the change;
#   * no vendor "noreply" addresses;
#   * no mention of a code-generation tool, an assistant or the vendor behind
#     one - its name, a model name, or a phrase crediting one with the work.
#
# The patterns below are assembled at run time from short fragments, so that
# this file does not itself contain the names or phrases it forbids and
# therefore passes the same check it applies (the self-test runs it over
# itself).
#
# Where it runs: CI (.github/workflows/commit-policy.yml, over the PR title,
# the PR body and every commit message in the PR range) and locally
# (tools/hooks/commit-msg, over the message being committed).
#
# Usage:
#   check_text_policy.ps1 -Text <string>... [-Label <name>]
#   check_text_policy.ps1 -Path <file>...   [-Label <name>] [-GitMessage]
#   <text> | check_text_policy.ps1 [-Stdin] [-Label <name>]
#
#   -GitMessage  The input is a commit message file as git hands it to the
#                commit-msg hook: lines starting with '#' are comments git will
#                strip, and everything from the scissors line (verbose commits)
#                is a diff and is skipped.
#
# Output:  <label>:<line>: <what was found> [<rule>]: <the line>   (stderr)
# Exit:    0 = clean, 1 = violation(s) found, 2 = bad invocation.

[CmdletBinding()]
param(
    [string[]]$Text,
    [string[]]$Path,
    [switch]$Stdin,
    [string]$Label,
    [switch]$GitMessage
)

$ErrorActionPreference = 'Stop'

function Join-Parts([string[]]$Parts) {
    return ($Parts -join '')
}

# --- the deny-list, from fragments -------------------------------------------

# One entry per forbidden name; each is a list of fragments that are joined
# (and may carry regex syntax). Matched case-insensitively, as whole words.
$nameParts = @(
    , @('cla', 'ude')
    , @('anth', 'ropic')
    , @('chat', '[- ]?', 'g', 'pt')
    , @('open', '\s?', 'a', 'i')
    , @('g', 'pt', '(?:-?\d\w*)?')
    , @('co', 'pilot')
    , @('gem', 'ini')
    , @('co', 'dex')
    , @('code', 'ium')
    , @('tab', 'nine')
    , @('wind', 'surf')
    , @('deep', 'seek')
    , @('gr', 'ok')
    , @('oll', 'ama')
    , @('ai', 'der')
    , @('cur', 'sor', '\s+(?:a', 'i|ide|agent|composer)')
)
$toolNamePattern = '\b(?:' + (($nameParts | ForEach-Object { Join-Parts $_ }) -join '|') + ')\b'

# The two-letter word for the field, spelled in pieces for the same reason.
$field = Join-Parts 'a', 'i'
$authorVerb = 'written|authored|created|produced|assisted|co-?authored'
$phrasePatterns = @(
    # The field's two-letter word as an adjective on an authorship verb.
    "\b$field[- ](?:generated|assisted|written|authored|created|co-?authored)\b",
    # An authorship verb, with / by / using, then the field, a model or a bot.
    "\b(?:$authorVerb)\s+(?:with|by|using)\s+(?:an?\s+)?(?:$field|llm|chat-?bot|assistant)\b"
)
$phrasePattern = '(?:' + ($phrasePatterns -join '|') + ')'

# The generic attribution forms, spelled in pieces like the names above.
$coAuthorPattern = '\b' + (Join-Parts 'co-?', 'author', 'ed-?', 'by') + '\b'
$generatedPattern = '\b' + (Join-Parts 'gener', 'ated', '\s+(?:wi', 'th|b', 'y|us', 'ing)') + '\b'

# The robot face that attribution footers like to start with.
$robot = [regex]::Escape([char]::ConvertFromUtf32(0x1F916))

$options = [System.Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
    [System.Text.RegularExpressions.RegexOptions]::CultureInvariant

$rules = @(
    [pscustomobject]@{
        Id      = 'co-author'
        Message = 'a co-author trailer or mention'
        Regex   = [regex]::new($coAuthorPattern, $options)
    },
    [pscustomobject]@{
        Id      = 'generated'
        Message = 'a line crediting a tool with having generated the text'
        Regex   = [regex]::new($generatedPattern, $options)
    },
    [pscustomobject]@{
        Id      = 'noreply-address'
        Message = 'a noreply address (a vendor attribution address)'
        # A person's own GitHub address (<id>+<name>@users.noreply.github.com)
        # has "noreply" in the domain, not before the @, so it does not match.
        Regex   = [regex]::new('(?<![A-Za-z0-9])no-?reply@[A-Za-z0-9.-]+', $options)
    },
    [pscustomobject]@{
        Id      = 'tool-name'
        Message = 'the name of a code-generation tool, assistant or vendor'
        Regex   = [regex]::new($toolNamePattern, $options)
    },
    [pscustomobject]@{
        Id      = 'tool-phrase'
        Message = 'a phrase crediting a tool or assistant with the work'
        Regex   = [regex]::new($phrasePattern, $options)
    },
    [pscustomobject]@{
        Id      = 'robot-emoji'
        Message = 'a robot emoji (an attribution marker)'
        Regex   = [regex]::new($robot, $options)
    }
)

# --- what to check ------------------------------------------------------------

$sources = [System.Collections.Generic.List[object]]::new()

function Add-Source([string]$Name, [string]$Content) {
    $sources.Add([pscustomobject]@{ Name = $Name; Content = $Content })
}

if ($PSBoundParameters.ContainsKey('Text')) {
    $index = 0
    foreach ($value in $Text) {
        $index++
        $base = if ($Label) { $Label } else { 'text' }
        $name = if (@($Text).Count -gt 1) { "$base[$index]" } else { $base }
        Add-Source $name ([string]$value)
    }
}

if ($PSBoundParameters.ContainsKey('Path')) {
    foreach ($file in $Path) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            [Console]::Error.WriteLine("check_text_policy: no such file: $file")
            exit 2
        }
        $name = if ($Label) { $Label } else { $file }
        Add-Source $name ([System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $file).Path))
    }
}

$readStdin = $Stdin.IsPresent
if (-not $PSBoundParameters.ContainsKey('Text') -and
    -not $PSBoundParameters.ContainsKey('Path') -and
    -not $readStdin) {
    if ([Console]::IsInputRedirected) {
        $readStdin = $true
    } else {
        [Console]::Error.WriteLine('check_text_policy: give -Text, -Path or text on stdin (see the header of this script).')
        exit 2
    }
}
if ($readStdin) {
    $name = if ($Label) { $Label } else { '<stdin>' }
    Add-Source $name ([Console]::In.ReadToEnd())
}

# --- check ---------------------------------------------------------------------

# The line git writes above the diff of a verbose commit.
$scissors = '^# -{24} >8 -{24}\s*$'

function ConvertTo-AnnotationText([string]$value) {
    return $value.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
}

$violations = 0
foreach ($source in $sources) {
    $lineNumber = 0
    foreach ($line in ($source.Content -split '\r?\n')) {
        $lineNumber++
        if ($GitMessage) {
            if ($line -match $scissors) { break }
            if ($line.StartsWith('#')) { continue }
        }
        foreach ($rule in $rules) {
            $match = $rule.Regex.Match($line)
            if (-not $match.Success) { continue }
            $violations++
            $shown = $line.Trim()
            if ($shown.Length -gt 160) { $shown = $shown.Substring(0, 157) + '...' }
            # Always starts with the label, never with "::", so text taken
            # from a pull request cannot be read as a workflow command.
            [Console]::Error.WriteLine(('{0}:{1}: {2} [{3}]: {4}' -f $source.Name, $lineNumber, $rule.Message, $rule.Id, $shown))
            if ($env:GITHUB_ACTIONS -eq 'true') {
                $annotation = ConvertTo-AnnotationText ('{0}, line {1}: {2}' -f $source.Name, $lineNumber, $rule.Message)
                Write-Host "::error title=Text policy::$annotation"
            }
            # One finding per line is enough to act on.
            break
        }
    }
}

if ($violations -gt 0) {
    [Console]::Error.WriteLine('')
    [Console]::Error.WriteLine(('Text policy: {0} violation(s). Commit messages, PR titles and PR bodies carry no attribution:' -f $violations))
    [Console]::Error.WriteLine('no co-author trailers, no lines crediting a tool with the work, no tool, assistant or vendor names.')
    [Console]::Error.WriteLine('Reword the text and try again (docs/ci.md, "Commit and PR text policy").')
    exit 1
}

exit 0
