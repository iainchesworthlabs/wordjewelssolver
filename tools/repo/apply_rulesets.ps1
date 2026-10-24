#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-3.0-or-later
#requires -Version 7.0
<#
.SYNOPSIS
Creates or updates this repository's rulesets from .github/rulesets/*.json.

.DESCRIPTION
The rulesets are kept in the repository as the JSON bodies the GitHub REST API
takes ("Create a repository ruleset"). This script is the one way they get to
GitHub: for each file it finds the ruleset with the same name and updates it, or
creates it when there is none - so it is safe to run again.

It is a DRY RUN unless -Apply is given: it reads (GET only), says what it would
send, and sends nothing.

Rulesets are not available on every repository. On GitHub Free they exist only
for PUBLIC repositories; a private repository needs a paid plan, and the API
answers 403 ("Upgrade to GitHub Pro or make this repository public"). The
script asks first and stops with an explanation when they are not available -
see docs/branch-protection.md, "Applying the rulesets". The moment to run it is
when the repository is made public.

.PARAMETER Repo
owner/name. Default: the repository the current directory's remote names
(gh repo view).

.PARAMETER Only
Ruleset names (or file base names) to handle; default: all of them.

.PARAMETER Apply
Really create or update. Without it nothing is sent.

.PARAMETER Offline
Print the payloads without calling the API at all (no capability check, no
comparison with what exists). Cannot be combined with -Apply.

.PARAMETER RulesetsDir
Where the JSON files are. Default: .github/rulesets of this checkout.

.NOTES
Exit: 0 = done (or dry run complete), 2 = bad invocation or bad ruleset file,
3 = rulesets are not available on this repository, 4 = an API call failed.
Needs the GitHub CLI (gh), authenticated as a repository admin.

.EXAMPLE
./tools/repo/apply_rulesets.ps1                 # dry run
./tools/repo/apply_rulesets.ps1 -Apply          # create or update
./tools/repo/apply_rulesets.ps1 -Offline        # just print the payloads
#>
[CmdletBinding()]
param(
    [string]$Repo,
    [string[]]$Only,
    [switch]$Apply,
    [switch]$Offline,
    [string]$RulesetsDir
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (-not $RulesetsDir) { $RulesetsDir = Join-Path $repoRoot '.github/rulesets' }

function Stop-Script([int]$Code, [string]$Message) {
    [Console]::Error.WriteLine($Message)
    exit $Code
}

if ($Apply -and $Offline) {
    Stop-Script 2 '-Apply and -Offline cannot be combined: -Offline makes no API calls.'
}
if (-not (Test-Path -LiteralPath $RulesetsDir -PathType Container)) {
    Stop-Script 2 "No such directory: $RulesetsDir"
}

# --- the local rulesets ------------------------------------------------------

$local = [System.Collections.Generic.List[object]]::new()
foreach ($file in (Get-ChildItem -LiteralPath $RulesetsDir -Filter '*.json' -File | Sort-Object Name)) {
    try {
        $body = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json -AsHashtable -Depth 64
    } catch {
        Stop-Script 2 "$($file.Name): not valid JSON: $($_.Exception.Message)"
    }
    foreach ($key in 'name', 'target', 'enforcement', 'rules') {
        if (-not $body.ContainsKey($key)) { Stop-Script 2 "$($file.Name): missing required key '$key'." }
    }
    if ($local | Where-Object { $_.Name -eq $body.name }) {
        Stop-Script 2 "$($file.Name): the ruleset name '$($body.name)' is used by another file."
    }
    $local.Add([pscustomobject]@{ Name = [string]$body.name; File = $file.FullName; Body = $body })
}
if ($Only) {
    $wanted = $Only
    $local = [System.Collections.Generic.List[object]]@($local | Where-Object {
            $wanted -contains $_.Name -or $wanted -contains [System.IO.Path]::GetFileNameWithoutExtension($_.File)
        })
}
if ($local.Count -eq 0) { Stop-Script 2 'No ruleset files selected.' }

function Write-Plan([string]$Heading, [object]$Item) {
    Write-Host ''
    Write-Host $Heading -ForegroundColor Cyan
    Write-Host (Get-Content -LiteralPath $Item.File -Raw).TrimEnd()
}

# --- offline: print and stop -------------------------------------------------

if ($Offline) {
    Write-Host 'OFFLINE: no API call is made; these are the payloads that would be sent by name.' -ForegroundColor Yellow
    foreach ($item in $local) { Write-Plan "ruleset '$($item.Name)' ($($item.File))" $item }
    exit 0
}

# --- gh ----------------------------------------------------------------------

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Stop-Script 2 'The GitHub CLI (gh) is not on PATH. Install it from https://cli.github.com/ and run `gh auth login`.'
}

function Invoke-Gh([string[]]$GhArguments) {
    # 2>&1: gh prints an HTTP error's message on stderr and its JSON body on
    # stdout; both are wanted in one string.
    $output = & gh @GhArguments 2>&1
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output   = (($output | ForEach-Object { "$_" }) -join "`n")
    }
}

if (-not $Repo) {
    Push-Location $repoRoot
    try { $named = Invoke-Gh @('repo', 'view', '--json', 'nameWithOwner', '--jq', '.nameWithOwner') } finally { Pop-Location }
    if ($named.ExitCode -ne 0 -or -not $named.Output) {
        Stop-Script 2 "Could not work out the repository from the current directory's remote. Pass -Repo owner/name.`n$($named.Output)"
    }
    $Repo = $named.Output.Trim()
}

# --- can this repository have rulesets? --------------------------------------

$repoCall = Invoke-Gh @('api', "repos/$Repo")
if ($repoCall.ExitCode -ne 0) {
    Stop-Script 4 "Could not read $Repo (is gh authenticated, and is the name right?):`n$($repoCall.Output)"
}
$repoInfo = $repoCall.Output | ConvertFrom-Json
$visibility = if ($repoInfo.private) { 'private' } else { 'public' }
Write-Host "Repository: $Repo ($visibility, owner type $($repoInfo.owner.type))"
if (-not $repoInfo.permissions.admin) {
    Stop-Script 4 "The authenticated user is not an admin of $Repo; creating rulesets needs admin rights."
}

# Asking for the list is the capability check: it answers 403 with an upgrade
# message where rulesets are not part of the plan, whatever the plan is called.
# includes_parents=false: only this repository's own rulesets are candidates
# for an update.
$listCall = Invoke-Gh @('api', "repos/$Repo/rulesets?includes_parents=false&per_page=100")
if ($listCall.ExitCode -ne 0) {
    if ($listCall.Output -match 'HTTP 403|Upgrade to GitHub|upgrade') {
        $message = @"

Rulesets are not available on $Repo ($visibility).

GitHub answered: $(($listCall.Output -split "`n" | Where-Object { $_ } | Select-Object -Last 1))

On GitHub Free, rulesets, branch protection and the merge queue exist only for
PUBLIC repositories; a private repository needs a paid plan (GitHub Pro for a
personal account, Team or Enterprise for an organisation). Nothing was sent.

Until then the workflows (ci.yml, branch-name-check.yml, commit-policy.yml) and
the local hooks (tools/hooks) are the enforcement. Run this script with -Apply
at the moment the repository is made public. To review the payloads in the
meantime:  ./tools/repo/apply_rulesets.ps1 -Offline
See docs/branch-protection.md, "Applying the rulesets".
"@
        [Console]::Error.WriteLine($message)
        exit 3
    }
    Stop-Script 4 "Could not list the rulesets of ${Repo}:`n$($listCall.Output)"
}
$existing = @($listCall.Output | ConvertFrom-Json)

# --- compare, then create or update -----------------------------------------

# True when every key and value of $Wanted is present, equal, in $Have. GitHub
# fills in defaults and bookkeeping (ids, links, defaulted parameters) that the
# local file does not mention; those are not differences.
function Test-Contains($Wanted, $Have) {
    if ($Wanted -is [System.Collections.IDictionary]) {
        if ($Have -isnot [System.Collections.IDictionary] -and $Have -isnot [pscustomobject]) { return $false }
        foreach ($key in $Wanted.Keys) {
            # Plain assignments, not an if-expression: an empty array coming
            # out of one would be unrolled to $null.
            if ($Have -is [System.Collections.IDictionary]) {
                if (-not $Have.Contains($key)) { return $false }
                $haveValue = $Have[$key]
            } else {
                if ($null -eq $Have.PSObject.Properties[$key]) { return $false }
                $haveValue = $Have.$key
            }
            if (-not (Test-Contains $Wanted[$key] $haveValue)) { return $false }
        }
        return $true
    }
    if ($Wanted -is [System.Collections.IList]) {
        if ($Have -isnot [System.Collections.IList]) { return $false }
        if ($Wanted.Count -ne $Have.Count) { return $false }
        for ($i = 0; $i -lt $Wanted.Count; $i++) {
            if (-not (Test-Contains $Wanted[$i] $Have[$i])) { return $false }
        }
        return $true
    }
    if ($Have -is [System.Collections.IList] -or $Have -is [System.Collections.IDictionary] -or $Have -is [pscustomobject]) {
        return $false
    }
    return ($Wanted -eq $Have)
}

if ($Apply) {
    Write-Host 'APPLY: rulesets will be created or updated.' -ForegroundColor Yellow
} else {
    Write-Host 'DRY RUN: nothing is sent. Re-run with -Apply to create or update.' -ForegroundColor Yellow
}

$failed = $false
foreach ($item in $local) {
    $match = @($existing | Where-Object { $_.name -eq $item.Name })
    if ($match.Count -gt 1) {
        Stop-Script 4 "More than one ruleset named '$($item.Name)' exists on $Repo; resolve that by hand."
    }

    if ($match.Count -eq 0) {
        $method = 'POST'
        $endpoint = "repos/$Repo/rulesets"
        Write-Plan "CREATE ruleset '$($item.Name)': POST $endpoint" $item
    } else {
        $id = $match[0].id
        $detail = Invoke-Gh @('api', "repos/$Repo/rulesets/$id")
        if ($detail.ExitCode -ne 0) { Stop-Script 4 "Could not read ruleset $id ('$($item.Name)'):`n$($detail.Output)" }
        $current = $detail.Output | ConvertFrom-Json
        if (Test-Contains $item.Body $current) {
            Write-Host ''
            Write-Host "UNCHANGED ruleset '$($item.Name)' (id $id): already as the file says." -ForegroundColor Green
            continue
        }
        $method = 'PUT'
        $endpoint = "repos/$Repo/rulesets/$id"
        Write-Plan "UPDATE ruleset '$($item.Name)' (id $id): PUT $endpoint" $item
    }

    if (-not $Apply) { continue }

    $call = Invoke-Gh @('api', '--method', $method, $endpoint, '--input', $item.File)
    if ($call.ExitCode -ne 0) {
        $failed = $true
        [Console]::Error.WriteLine("FAILED ruleset '$($item.Name)':`n$($call.Output)")
        continue
    }
    $result = $call.Output | ConvertFrom-Json
    Write-Host "DONE   ruleset '$($item.Name)' (id $($result.id)): $($result._links.html.href)" -ForegroundColor Green
}

if ($failed) { exit 4 }
if (-not $Apply) {
    Write-Host ''
    Write-Host 'Dry run complete: nothing was sent.' -ForegroundColor Yellow
}
exit 0
