<#
review-runner.ps1 - review exactly one request, then stop.

The runner is given a claimed manifest. It:
  1. builds a detached worktree pinned to REVIEW_HEAD, so the review target
     cannot drift while the implementer keeps committing on the branch,
  2. writes a review packet (fixed BASE..HEAD diff, commit list, evidence),
  3. starts the reviewer once, with only the options this installation proved
     it has in .ai-runtime/logs/codex-interface.json,
  4. checks that the reviewer changed nothing,
  5. writes a review result next to the packet and appends to the ledger,
  6. removes the worktree.

It never starts a reviewer on the current branch, and it never starts a second
one for the same request.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ManifestPath,
    [string]$RepoRoot,
    [switch]$DryRun,
    [int]$TimeoutSeconds = 0,
    [switch]$Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')
. (Join-Path $PSScriptRoot 'review-profile.ps1')

$RepoRoot = Get-RepoRoot -Hint $RepoRoot
$paths = Initialize-AiRuntime -RepoRoot $RepoRoot
$gitExe = Resolve-GitExe

function Say {
    param([string]$Message, [string]$Level = 'INFO')
    Write-AiLog -Message ("runner: " + $Message) -Level $Level -LogPath $paths.Dispatcher -Quiet:$Quiet
}

$manifest = Read-JsonFile -Path $ManifestPath
if ($null -eq $manifest) {
    Say "manifest is not readable JSON: $ManifestPath" 'ERROR'
    exit 2
}
function Get-ManifestValue {
    param([string]$Name, $Default = '')
    foreach ($p in $manifest.PSObject.Properties) {
        if ($p.Name -eq $Name -and $null -ne $p.Value) { return $p.Value }
    }
    return $Default
}

$requestId = [string]$manifest.request_id
if (-not (Test-SafeRequestId -RequestId $requestId)) {
    Write-AiLog -Message ("runner: '" + $requestId + "' is not a plain name; nothing is started") `
        -Level 'ERROR' -LogPath $paths.Dispatcher -Quiet:$Quiet
    exit 2
}
$rootId = Get-RootRequestId -RequestId $requestId
$reviewCommit = [string]$manifest.review_commit
$baseCommit = [string]$manifest.base_commit

if ($DryRun.IsPresent -eq $false -and (Test-AlreadyReviewed -RepoRoot $RepoRoot -RequestId $requestId)) {
    Say "$requestId already has a review recorded; refusing to review it twice" 'WARN'
    exit 3
}

# The profile decided at enqueue time carries its own patience. A documentation
# fix that has not answered in ten minutes is stuck; a dangerous change deserves
# longer. Nothing here is "wait an hour and hope".
$reviewProfile = [string](Get-ManifestValue 'review_profile' 'NORMAL')
$reviewEffort = ConvertTo-KnownEffort -Effort ([string](Get-ManifestValue 'review_effort' 'medium'))
if ($TimeoutSeconds -le 0) {
    $TimeoutSeconds = [int](Get-ManifestValue 'timeout_seconds' 1200)
}

# The precheck ran when the request was accepted. Between then and now other
# requests may have been accepted too, and one of them may have made this one
# wrong: the same commit already reviewed, or a third block in a row on this root.
# So it is asked again, here, immediately before anything is started.
if (-not $DryRun) {
    $recheck = ''
    try {
        $recheck = (& (Join-Path $PSScriptRoot 'review-precheck.ps1') `
            -RepoRoot $RepoRoot -ManifestPath $ManifestPath 2>&1 | Out-String)
    } catch { $recheck = ('the precheck threw: ' + $_.Exception.Message) }
    $recheckResult = ConvertFrom-JsonLoose -Text $recheck
    if ($null -eq $recheckResult -or -not [bool]$recheckResult.ok) {
        $why = 'the precheck gave no answer it could read back'
        if ($recheckResult) {
            $why = [string]$recheckResult.message
        } elseif ($recheck) {
            $excerptLength = [Math]::Min(1200, $recheck.Length)
            $why = $why + ': ' + $recheck.Substring(0, $excerptLength)
        }
        Say ("$requestId is no longer fit to review: " + $why) 'WARN'
        exit 7
    }
}

$workDir = Join-Path $paths.Processing $requestId
if (-not (Test-Path -LiteralPath $workDir)) {
    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
}
# The packet goes INSIDE the pinned worktree. A reviewer running read-only in that
# directory can always read it; a path somewhere else on the disk may be outside
# what its sandbox allows, and then the review fails for no good reason.
$packetDirName = '.ai-review-packet'
$lastMsgPath  = Join-Path $workDir 'reviewer-last-message.txt'
$stdoutPath   = Join-Path $workDir 'reviewer-stdout.txt'
$resultJson   = Join-Path $paths.Results ($requestId + '.json')
$resultText   = Join-Path $paths.Results ($requestId + '.md')

function Get-WorktreeRoot {
    if ($env:KACHA_AI_WORKTREE_ROOT) { return $env:KACHA_AI_WORKTREE_ROOT }
    $sibling = Join-Path (Split-Path -Parent $RepoRoot) 'kachakachaCAD-worktrees'
    if (Test-Path -LiteralPath $sibling) { return (Join-Path $sibling 'ai-review') }
    return $paths.Worktrees
}

$worktreeRoot = Get-WorktreeRoot
if (-not (Test-Path -LiteralPath $worktreeRoot)) {
    New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
}
$worktreePath = Join-Path $worktreeRoot $requestId

function Remove-ReviewWorktree {
    param([string]$Path)
    if (-not $Path) { return }
    # Only ever the throwaway review worktree, and only ever a direct child of the
    # worktree root, compared after the path has been resolved. A string test alone
    # lets "<root>\..\somewhere-else" through, and this call deletes recursively.
    if (-not (Test-PathIsDirectChildOf -Path $Path -Root $worktreeRoot)) {
        Say ("refusing to remove " + $Path + ": it is not a review worktree under " + $worktreeRoot) 'ERROR'
        return
    }
    if (-not (Test-Path -LiteralPath $Path)) { return }
    # Git has to agree that this is one of its worktrees for this repository.
    # A directory that merely sits in the right place, or one another process
    # has locked, is not ours to delete recursively.
    $listed = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'list', '--porcelain')
    $known = $false
    $full = ''
    try { $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/').ToLowerInvariant() } catch { }
    foreach ($line in ($listed.StdOut -split "`r?`n")) {
        if ($line -notlike 'worktree *') { continue }
        $candidate = $line.Substring(9).Trim()
        try { $candidate = [System.IO.Path]::GetFullPath($candidate).TrimEnd('\', '/').ToLowerInvariant() } catch { continue }
        if ($candidate -eq $full) { $known = $true; break }
    }
    if (-not $known) {
        Say ("refusing to remove " + $Path + ": git does not list it as a worktree of this repository") 'ERROR'
        return
    }
    $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'remove', '--force', $Path)
    if ($r.ExitCode -ne 0 -and (Test-Path -LiteralPath $Path)) {
        # Git refused. Something is holding it, or it is locked. Set it aside
        # rather than deleting a directory whose state we do not understand.
        $parked = Join-Path $paths.Stale ((Split-Path -Leaf $Path) + '-' + (Get-Date).ToString('yyyyMMdd-HHmmss'))
        try {
            Move-Item -LiteralPath $Path -Destination $parked -Force
            Say ("git would not remove " + $Path + " (" + $r.StdErr.Trim() + "); it was moved to " + $parked) 'WARN'
        } catch {
            Say ("git would not remove " + $Path + " and it could not be moved aside: " + $_.Exception.Message) 'ERROR'
        }
    }
    Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'prune') | Out-Null
}

function Write-ReviewResult {
    param(
        [string]$Verdict,
        [string]$NextAction,
        [int]$BlockingCount,
        [int]$ExitCode,
        [string]$ReviewerCommand,
        [string]$StartedUtc,
        [string[]]$Notes,
        [string]$Reviewer = 'codex',
        [string]$LedgerEvent = 'review_completed',
        [string]$Outcome = 'REVIEWED',
        $Invocation = $null
    )
    $finished = Get-UtcStamp
    $streak = Get-ConsecutiveBlockingCount -RepoRoot $RepoRoot -RootRequestId $rootId
    # What this result does to the streak. A timeout, an infrastructure failure or
    # an answer in the wrong shape says nothing about the change, so it leaves the
    # count where it was. Only a real BLOCKING adds to it, and a PASS clears it.
    $streakAfter = $streak
    if ($Outcome -eq 'REVIEWED') {
        if ($Verdict -eq 'BLOCKING') { $streakAfter = $streak + 1 }
        elseif ($Verdict -eq 'PASS') { $streakAfter = 0 }
    }
    $effectiveNext = $NextAction
    # Only a real review can be part of a blocking streak. Three timeouts in a row
    # are an infrastructure problem, not three rejections.
    # STOP is already a handover to a person; it does not wait for a third strike.
    if ($Outcome -eq 'REVIEWED' -and $NextAction -eq 'STOP') {
        $Notes += "the reviewer answered STOP; this goes to a person now, not after three tries"
    }
    if ($Outcome -eq 'REVIEWED' -and $Verdict -eq 'BLOCKING' -and $streakAfter -ge 3) {
        $effectiveNext = 'HUMAN_DECISION_REQUIRED'
        $Notes += "three blocking results in a row on $rootId; a person has to decide"
    }
    $result = [ordered]@{
        schema_version       = 1
        kind                 = 'review_result'
        request_id           = $requestId
        root_request_id      = $rootId
        attempt              = (Get-RequestAttempt -RequestId $requestId)
        base_commit          = $baseCommit
        review_commit        = $reviewCommit
        tested_commit        = [string]$manifest.tested_commit
        reviewer             = $Reviewer
        reviewer_command     = $ReviewerCommand
        started_utc          = $StartedUtc
        finished_utc         = $finished
        outcome              = $Outcome
        verdict              = $Verdict
        next_action          = $effectiveNext
        review_profile       = $reviewProfile
        review_effort        = $reviewEffort
        invocation           = $Invocation
        blocking_count       = $BlockingCount
        consecutive_blocking = $streakAfter
        exit_code            = $ExitCode
        review_text_file     = $resultText
        packet_file          = (Join-Path $workDir 'packet.md')
        worktree             = $worktreePath
        notes                = @($Notes)
        processed_by_claude  = $false
    }

    Write-JsonAtomic -Path $resultJson -Value ([pscustomobject]$result) | Out-Null
    # An infrastructure failure is not a verdict. Only a real review closes a
    # REQUEST_ID; otherwise the same id could never be tried again.
    # This line decides whether the REQUEST_ID is spent. Losing it would let the
    # same id be used again and overwrite a result that already exists, so a
    # failure here stops the runner instead of being shrugged off.
    Add-ReviewLedgerEntry -Required -RepoRoot $RepoRoot -Entry @{
        event = $LedgerEvent; request_id = $requestId; root_request_id = $rootId
        base_commit = $baseCommit; review_commit = $reviewCommit
        outcome = $Outcome; verdict = $Verdict; next_action = $effectiveNext
        review_profile = $reviewProfile; review_effort = $reviewEffort
        blocking_count = $BlockingCount; reviewer = $Reviewer; exit_code = $ExitCode
        note = (@($Notes) -join '; ')
    } | Out-Null
    return $result
}

# ------------------------------------------------------------- worktree -----
# A detached worktree at a fixed commit. The branch may move under us during the
# review; this checkout cannot.
Remove-ReviewWorktree -Path $worktreePath
$add = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @(
    'worktree', 'add', '--detach', $worktreePath, $reviewCommit)
if ($add.ExitCode -ne 0) {
    Say ("could not create the review worktree: " + $add.StdErr.Trim()) 'ERROR'
    Write-TextAtomic -Path $resultText -Text ("review worktree could not be created:`n" + $add.StdErr) | Out-Null
    Write-ReviewResult -Verdict 'INFRA_ERROR' -NextAction 'HUMAN_DECISION_REQUIRED' -BlockingCount 0 `
        -ExitCode $add.ExitCode -ReviewerCommand '' -StartedUtc (Get-UtcStamp) `
        -Notes @('git worktree add failed') -LedgerEvent 'review_infra_error' -Outcome 'INFRA_ERROR' | Out-Null
    exit 5
}

$packetDir    = Join-Path $worktreePath $packetDirName
New-Item -ItemType Directory -Path $packetDir -Force | Out-Null
$packetPath   = Join-Path $packetDir 'packet.md'
$diffPath     = Join-Path $packetDir 'diff.patch'
$statPath     = Join-Path $packetDir 'diffstat.txt'
$commitsPath  = Join-Path $packetDir 'commits.txt'

# ---------------------------------------------------------------- packet ----

# A request may narrow the packet to certain paths. HEAD stays what the machine
# really built, so the evidence still matches; only the part put in front of the
# reviewer is narrowed, and the packet says so out loud.
$pathFilter = @()
foreach ($p in $manifest.PSObject.Properties) {
    if ($p.Name -eq 'paths' -and $p.Value) {
        foreach ($item in @($p.Value)) { if ($item) { $pathFilter += [string]$item } }
    }
}
$pathArguments = @()
if ($pathFilter.Count -gt 0) { $pathArguments = @('--') + $pathFilter }

$commits = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments (@(
    'log', '--no-color', '--oneline', ($baseCommit + '..' + $reviewCommit)) + $pathArguments)
$stat = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments (@(
    'diff', '--no-color', '--stat', $baseCommit, $reviewCommit) + $pathArguments)
$diff = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments (@(
    'diff', '--no-color', $baseCommit, $reviewCommit) + $pathArguments)

# If any of those failed, the packet would be empty and the reviewer would
# honestly report that nothing changed. An empty packet is not a clean change.
$packetProblems = @()
if ($commits.ExitCode -ne 0) { $packetProblems += ('git log failed: ' + $commits.StdErr.Trim()) }
if ($stat.ExitCode -ne 0)    { $packetProblems += ('git diff --stat failed: ' + $stat.StdErr.Trim()) }
if ($diff.ExitCode -ne 0)    { $packetProblems += ('git diff failed: ' + $diff.StdErr.Trim()) }
if ($diff.ExitCode -eq 0 -and $diff.StdOut.Trim().Length -eq 0) {
    $packetProblems += 'the fixed diff is empty; there is nothing to review in this range'
}
if ($packetProblems.Count -gt 0) {
    $why = ($packetProblems -join '; ')
    Say ("the review packet could not be built: " + $why) 'ERROR'
    Write-TextAtomic -Path $resultText -Text ("review packet could not be built:`n" + $why) | Out-Null
    Write-ReviewResult -Verdict 'INFRA_ERROR' -NextAction 'HUMAN_DECISION_REQUIRED' -BlockingCount 0 `
        -ExitCode 1 -ReviewerCommand '' -StartedUtc (Get-UtcStamp) `
        -Notes @($packetProblems) -Reviewer 'none' `
        -LedgerEvent 'review_infra_error' -Outcome 'INFRA_ERROR' | Out-Null
    Remove-ReviewWorktree -Path $worktreePath
    exit 6
}

Write-TextAtomic -Path $commitsPath -Text $commits.StdOut -WithBom | Out-Null
Write-TextAtomic -Path $statPath -Text $stat.StdOut -WithBom | Out-Null
Write-TextAtomic -Path $diffPath -Text $diff.StdOut -WithBom | Out-Null

$focusText = ''
foreach ($p in $manifest.PSObject.Properties) {
    if ($p.Name -eq 'focus' -and $p.Value) { $focusText = (@($p.Value) -join "`n- ") }
}
$scopeText = ''
foreach ($p in $manifest.PSObject.Properties) {
    if ($p.Name -eq 'scope_ja') { $scopeText = [string]$p.Value }
}

$packet = @"
# Review packet $requestId

REQUEST_ID: $requestId
ROOT_REQUEST_ID: $rootId
BASE: $baseCommit
HEAD: $reviewCommit
BRANCH_AT_TEST: $($manifest.branch_at_test)
REVIEW_EFFORT: $($manifest.review_effort)

## Machine evidence (already verified on this machine, do not re-run)

- build: $($manifest.build_result)
- ctest: $($manifest.test_result) $($manifest.evidence.ctest)
- application self-test: $($manifest.selftest_result) $($manifest.evidence.selftest)
- log: $($manifest.evidence.log)

## Scope

$scopeText

## Focus

- $focusText

## Path filter

$(if ($pathFilter.Count -gt 0) { 'This packet covers only: ' + ($pathFilter -join ', ') +
  '. Changes to other paths in this range are deliberately out of scope for this request.' }
  else { 'None: the whole BASE..HEAD range is in scope.' })

## Files in this packet

- commits.txt  : commit list BASE..HEAD
- diffstat.txt : changed files
- diff.patch   : the complete fixed diff BASE..HEAD

The working tree you are in is a detached checkout pinned to HEAD. It will be
deleted after the review. Do not edit it.

The rules for your answer are in docs/ai/CODEX_REVIEW_POLICY.md in this tree.
"@
Write-TextAtomic -Path $packetPath -Text $packet -WithBom | Out-Null

# ------------------------------------------------------------- reviewer ----

# Always ask the precheck rather than reading the cached answer directly: only the
# precheck knows whether that answer was written by the current set of checks.
& (Join-Path $PSScriptRoot 'review-precheck.ps1') -RepoRoot $RepoRoot -Machine -Quiet | Out-Null
$interface = Read-JsonFile -Path $paths.Interface
$fallbackInterface = Read-JsonFile -Path (Join-Path $paths.Logs 'claude-interface.json')

# Which reviewer is actually going to run. Codex is the reviewer; the fallback is
# only reached when Codex cannot run here, and it is named as a fallback in the
# result so that nobody later mistakes it for an independent Codex review.
$reviewerKind = 'codex'
$reviewerExe = ''
$reviewerFlags = @()
$reviewerVersion = ''
if ($null -ne $interface -and $interface.probe_ok) {
    $reviewerExe = [string]$interface.executable
    $reviewerVersion = [string]$interface.version
    foreach ($f in @($interface.supported_flags)) { $reviewerFlags += [string]$f }
} elseif ($null -ne $fallbackInterface -and $fallbackInterface.probe_ok) {
    $reviewerKind = 'claude-fallback'
    $reviewerExe = [string]$fallbackInterface.executable
    $reviewerVersion = [string]$fallbackInterface.version
    foreach ($f in @($fallbackInterface.supported_flags)) { $reviewerFlags += [string]$f }
    $codexNote = 'codex is not usable on this machine'
    if ($null -ne $interface) { $codexNote = [string]$interface.probe_note }
    Say ("codex cannot run here (" + $codexNote + "); using the sanctioned fallback reviewer") 'WARN'
}

if (-not $reviewerExe) {
    Say "no usable reviewer command on this machine; nothing is started" 'ERROR'
    Write-TextAtomic -Path $resultText -Text "reviewer unavailable on this machine" | Out-Null
    Write-ReviewResult -Verdict 'INFRA_ERROR' -NextAction 'HUMAN_DECISION_REQUIRED' -BlockingCount 0 `
        -ExitCode 127 -ReviewerCommand '' -StartedUtc (Get-UtcStamp) `
        -Notes @('no reviewer command on this machine; see .ai-runtime/logs/reviewer-search.json') -Reviewer 'none' `
        -LedgerEvent 'review_infra_error' -Outcome 'INFRA_ERROR' | Out-Null
    Remove-ReviewWorktree -Path $worktreePath
    exit 4
}

$flags = $reviewerFlags

# Everything the reviewer needs is in the prompt itself. It is never asked to
# work out what changed, to find the request, or to re-establish what the machine
# already proved. Every path is relative to the working tree it starts in, so
# nothing depends on what its sandbox lets it reach elsewhere.
$changedFileList = @()
foreach ($item in @(Get-ManifestValue 'changed_files' @())) { $changedFileList += [string]$item }
$shownFiles = $changedFileList
$fileTail = ''
if ($shownFiles.Count -gt 60) {
    $shownFiles = $shownFiles[0..59]
    $fileTail = " (and " + ($changedFileList.Count - 60) + " more, all listed in ./$packetDirName/diffstat.txt)"
}
$pathNote = 'the whole range'
if ($pathFilter.Count -gt 0) { $pathNote = 'only these paths: ' + ($pathFilter -join ', ') }

# All of it goes into one file inside the reviewer's own working tree, and the
# command line stays a single short line. A prompt with newlines in it does not
# survive a .cmd shim, and a long file list does not survive a command line length
# limit; a file in the working tree survives both.
$requestPath = Join-Path $packetDir 'request.md'
$requestText = @"
# Review request $requestId

You are reviewing one fixed change for kachakachaCAD. Everything you need is here.

REQUEST_ID: $requestId
BASE: $baseCommit
HEAD: $reviewCommit
SCOPE: $pathNote
PROFILE: $reviewProfile (reasoning effort $reviewEffort)

## Already proved on this machine - do not repeat any of it

- build: $($manifest.build_result)
- ctest: $($manifest.test_result) $($manifest.evidence.ctest)
- application self-test: $($manifest.selftest_result) $($manifest.evidence.selftest)

**Do not build. Do not run tests. Do not run the application.** They passed here
already, on this machine, before you were started.

## What to look at

$($manifest.scope_ja)

## Read in this order

1. ``./$packetDirName/diffstat.txt`` and ``./$packetDirName/diff.patch`` - the fixed
   diff, BASE..HEAD. Read this first and read all of it.
2. The changed tests inside that diff. Do they actually hold this change down?
3. Only then, other files, and only the ones the diff makes you need.

**Do not survey the repository.** Do not open files the diff does not point at.

## Changed files ($($changedFileList.Count))

$(($shownFiles -join "`n"))$fileTail

## The rules for your answer

``./docs/ai/CODEX_REVIEW_POLICY.md`` in this working tree. This is a read-only
review: do not modify any file. Your first non-empty line must be VERDICT.

## Reading these files

Every file in ``./$packetDirName/`` is UTF-8 and contains Japanese. If you read
one with Windows PowerShell, pass the encoding: ``Get-Content -Raw -Encoding UTF8``.
Without it PowerShell 5.1 falls back to the machine code page and the Japanese
arrives as mojibake.
"@
Write-TextAtomic -Path $requestPath -Text $requestText -WithBom | Out-Null

$prompt = "Read ./$packetDirName/request.md and do exactly what it says. Answer in the form it names, starting with VERDICT on the first non-empty line."

$arguments = @()
if ($reviewerKind -eq 'codex') {
    $arguments += 'exec'
    if ($flags -contains '-c' -and $reviewEffort) {
        $arguments += @('-c', ('model_reasoning_effort="' + $reviewEffort + '"'))
    }
    if ($flags -contains '--ephemeral')   { $arguments += '--ephemeral' }
    if ($flags -contains '--sandbox')     { $arguments += @('--sandbox', 'read-only') }
    if ($flags -contains '--output-last-message') { $arguments += @('--output-last-message', $lastMsgPath) }
    if ($flags -contains '-C')            { $arguments += @('-C', $worktreePath) }
    elseif ($flags -contains '--cd')      { $arguments += @('--cd', $worktreePath) }
} else {
    if ($flags -contains '-p')                    { $arguments += '-p' }
    elseif ($flags -contains '--print')           { $arguments += '--print' }
    if ($flags -contains '--permission-mode')     { $arguments += @('--permission-mode', 'plan') }
    if ($flags -contains '--permission-prompts')  { $arguments += @('--permission-prompts', 'none') }
}
$arguments += $prompt

$commandLine = ('{0} {1}' -f $reviewerExe, (ConvertTo-CommandLine -Arguments $arguments))

if ($DryRun) {
    Say "DRY_RUN for $requestId; the reviewer is not started"
    Write-TextAtomic -Path (Join-Path $workDir 'dry-run-command.txt') -Text $commandLine | Out-Null
    Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
        event = 'dry_run'; request_id = $requestId; root_request_id = $rootId
        base_commit = $baseCommit; review_commit = $reviewCommit
        note = $commandLine
    } | Out-Null
    Write-Output $commandLine
    Remove-ReviewWorktree -Path $worktreePath
    exit 0
}

$startedUtc = Get-UtcStamp
$queuedUtc = [string](Get-ManifestValue 'created_utc' '')
$waitSeconds = -1
if ($queuedUtc) {
    try { $waitSeconds = [int]((([datetime]$startedUtc) - ([datetime]$queuedUtc)).TotalSeconds) } catch { $waitSeconds = -1 }
}
$invocation = [ordered]@{
    reviewer            = $reviewerKind
    executable          = $reviewerExe
    version             = $reviewerVersion
    model               = [string](Get-ManifestValue 'model' '')
    reasoning_effort    = $reviewEffort
    review_profile      = $reviewProfile
    profile_reason      = [string](Get-ManifestValue 'profile_reason' '')
    command_line        = $commandLine
    base_commit         = $baseCommit
    review_commit       = $reviewCommit
    changed_file_count  = [int](Get-ManifestValue 'changed_file_count' 0)
    changed_lines       = [int](Get-ManifestValue 'changed_lines' 0)
    diff_bytes          = [int](Get-ManifestValue 'diff_bytes' 0)
    path_filter         = @($pathFilter)
    timeout_seconds     = $TimeoutSeconds
    queued_utc          = $queuedUtc
    started_utc         = $startedUtc
    queued_to_start_seconds = $waitSeconds
    finished_utc        = ''
    duration_seconds    = -1
}
Add-ReviewLedgerEntry -Required -RepoRoot $RepoRoot -Entry @{
    event = 'review_started'; request_id = $requestId; root_request_id = $rootId
    base_commit = $baseCommit; review_commit = $reviewCommit
    reviewer = $reviewerKind; review_profile = $reviewProfile; review_effort = $reviewEffort
    changed_file_count = $invocation.changed_file_count; changed_lines = $invocation.changed_lines
    diff_bytes = $invocation.diff_bytes; timeout_seconds = $TimeoutSeconds
    queued_to_start_seconds = $waitSeconds
    note = $commandLine
} | Out-Null
Say ("starting " + $reviewerKind + " once for " + $requestId + " at " + (Get-ShortSha $reviewCommit) +
     " profile=" + $reviewProfile + " effort=" + $reviewEffort +
     " files=" + $invocation.changed_file_count + " lines=" + $invocation.changed_lines +
     " diff=" + $invocation.diff_bytes + "B timeout=" + $TimeoutSeconds + "s" +
     " waited=" + $waitSeconds + "s")

# A file left by an earlier attempt would be read as this attempt's answer.
if (Test-Path -LiteralPath $lastMsgPath) { Remove-Item -LiteralPath $lastMsgPath -Force }

$run = $null
$notes = @()
try {
    $run = Invoke-Process -FilePath $reviewerExe -Arguments $arguments `
        -WorkingDirectory $worktreePath -TimeoutSeconds $TimeoutSeconds
} catch {
    $notes += ('the reviewer could not be started: ' + $_.Exception.Message)
}

$stdoutText = ''
$exitCode = 127
if ($run) {
    $stdoutText = $run.StdOut + "`n" + $run.StdErr
    $exitCode = $run.ExitCode
    if ($run.TimedOut) { $notes += "the reviewer was stopped after $TimeoutSeconds seconds" }
}
Write-TextAtomic -Path $stdoutPath -Text $stdoutText | Out-Null

$answer = ''
if (Test-Path -LiteralPath $lastMsgPath) {
    # Only a file this invocation created counts; the old one was removed above.
    $answer = [System.IO.File]::ReadAllText($lastMsgPath)
}
if (-not $answer -or $answer.Trim().Length -eq 0) { $answer = $stdoutText }

# The reviewer is read-only. If the pinned checkout came back dirty, that is
# reported; the change is dropped from the throwaway worktree only.
# The packet directory is ours, not the reviewer's doing, so it is not counted.
$dirty = Invoke-Git -RepoRoot $worktreePath -GitExe $gitExe -Arguments @('status', '--porcelain')
$touched = @()
if ($dirty.ExitCode -eq 0) {
    foreach ($line in ($dirty.StdOut -split "`r?`n")) {
        if ($line.Trim().Length -eq 0) { continue }
        if ($line -like ('*' + $packetDirName + '*')) { continue }
        $touched += $line
    }
}
if ($touched.Count -gt 0) {
    $notes += 'the reviewer modified files; a review is read-only, so the edits were dropped'
    Write-TextAtomic -Path (Join-Path $workDir 'reviewer-touched-files.txt') -Text ($touched -join "`n") | Out-Null
}

# ---------------------------------------------------------------- verdict ---

function Get-Field {
    param([string]$Text, [string]$Name)
    $m = [regex]::Match($Text, ('(?im)^\s*' + [regex]::Escape($Name) + '\s*:\s*(?<v>.+)$'))
    if ($m.Success) { return $m.Groups['v'].Value.Trim() }
    return ''
}

$verdict = Get-Field -Text $answer -Name 'VERDICT'
$nextAction = Get-Field -Text $answer -Name 'NEXT_ACTION'
$blockingText = Get-Field -Text $answer -Name 'BLOCKING_COUNT'
$blockingCount = 0
# A digit string can still be too big for an integer. Casting it would throw
# before the answer could be called MALFORMED, and the runner would die instead
# of writing down what went wrong.
$blockingCountIsNumber = $false
if ($blockingText -match '^\d+$') {
    $parsed = 0
    if ([int]::TryParse($blockingText, [ref]$parsed) -and $parsed -ge 0 -and $parsed -le 1000) {
        $blockingCountIsNumber = $true
        $blockingCount = $parsed
    }
}

# The contract says the FIRST non-empty line is the verdict. An answer that was
# cut off halfway, or that buries the verdict after a paragraph of preamble, is
# not the agreed form and must not be read as one.
$firstLine = ''
foreach ($line in ($answer -split "`r?`n")) {
    if ($line.Trim().Length -eq 0) { continue }
    $firstLine = $line.Trim()
    break
}

if (-not $verdict) {
    # Fall back to the old contract: first non-empty line is PASS or REVISE.
    foreach ($line in ($answer -split "`r?`n")) {
        $t = $line.Trim()
        if ($t.Length -eq 0) { continue }
        if ($t -match '^(PASS|REVISE|BLOCKING|STOP)\b') { $verdict = $Matches[1] }
        break
    }
}
if ($verdict -eq 'REVISE') { $verdict = 'BLOCKING' }

# The answer is checked against the contract, not merely searched for words that
# look like one. An unknown verdict, a pair that contradicts itself, or a count
# that disagrees with the verdict is not a result we can act on, and it must not
# quietly use up the REQUEST_ID.
$allowedVerdicts = @('PASS', 'BLOCKING', 'STOP')
$allowedNext = @{ 'PASS' = 'PROCEED'; 'BLOCKING' = 'FIX_AND_REVIEW'; 'STOP' = 'STOP' }
$contractProblems = @()
if ($firstLine -notlike 'VERDICT*') {
    $contractProblems += 'the first non-empty line is not VERDICT'
}
if (-not $verdict) {
    $contractProblems += 'the answer has no VERDICT line'
} elseif ($allowedVerdicts -notcontains $verdict) {
    $contractProblems += ("VERDICT is '" + $verdict + "', which is not PASS, BLOCKING or STOP")
} else {
    if (-not $nextAction) {
        $contractProblems += 'the answer has no NEXT_ACTION line'
    } elseif ($nextAction -ne $allowedNext[$verdict]) {
        $contractProblems += ("VERDICT " + $verdict + " does not go with NEXT_ACTION " + $nextAction)
    }
    if (-not $blockingText) {
        $contractProblems += 'the answer has no BLOCKING_COUNT line'
    } elseif (-not $blockingCountIsNumber) {
        $contractProblems += ("BLOCKING_COUNT is '" + $blockingText + "', which is not a whole number")
    } else {
        if ($verdict -eq 'BLOCKING' -and $blockingCount -lt 1) {
            $contractProblems += 'BLOCKING was answered with a BLOCKING_COUNT of zero'
        }
        if ($verdict -ne 'BLOCKING' -and $blockingCount -gt 0) {
            $contractProblems += ($verdict + ' was answered with blocking items')
        }
    }
}
if ($contractProblems.Count -gt 0) {
    $verdict = 'MALFORMED'
    $nextAction = 'HUMAN_DECISION_REQUIRED'
    foreach ($problem in $contractProblems) { $notes += ('the answer breaks the contract: ' + $problem) }
}

# A command that timed out, fell over, or never started did not review anything.
# That is not a failing review and it must never be turned into one. Each of these
# gets its own name, and none of them uses up the REQUEST_ID.
$outcome = 'REVIEWED'
$ledgerEvent = 'review_completed'
if ($null -eq $run) {
    $outcome = 'INFRA_ERROR'
    $ledgerEvent = 'review_infra_error'
    $notes += 'the reviewer could not be started at all'
} elseif ($run.TimedOut) {
    $outcome = 'TIMEOUT'
    $ledgerEvent = 'review_timeout'
    $notes += ("the reviewer was still going after " + $TimeoutSeconds + " seconds and was stopped")
} elseif ($exitCode -ne 0) {
    $outcome = 'RETRYABLE_ERROR'
    $ledgerEvent = 'review_retryable_error'
    $notes += ("the reviewer ended with exit " + $exitCode)
} elseif (-not $answer -or $answer.Trim().Length -eq 0) {
    $outcome = 'RETRYABLE_ERROR'
    $ledgerEvent = 'review_retryable_error'
    $notes += 'the reviewer ended without saying anything'
}
if ($outcome -ne 'REVIEWED') {
    $verdict = $outcome
    $nextAction = 'RETRY'
    if ($outcome -eq 'INFRA_ERROR') { $nextAction = 'HUMAN_DECISION_REQUIRED' }
    $notes += 'this is not a review result; the REQUEST_ID is not used up'
} elseif ($verdict -eq 'MALFORMED') {
    # The reviewer ran and answered, but not in a form anyone can act on. That is
    # a problem with the answer, not with the change, so a person looks at it and
    # the number is not spent on it.
    $ledgerEvent = 'review_malformed'
}
if ($verdict -eq 'BLOCKING' -and $blockingCount -eq 0) { $blockingCount = 1 }

$header = @"
## $requestId

REQUEST_ID: $requestId
BASE: $baseCommit
HEAD: $reviewCommit
SCOPE: $pathNote
PROFILE: $reviewProfile (effort $reviewEffort) - $(Get-ManifestValue 'profile_reason' '')
SIZE: $(Get-ManifestValue 'changed_file_count' 0) files, $(Get-ManifestValue 'changed_lines' 0) changed lines
REVIEWER: $reviewerKind ($reviewerVersion)
STARTED: $startedUtc
OUTCOME: $outcome
VERDICT: $verdict
NEXT_ACTION: $nextAction

"@
Write-TextAtomic -Path $resultText -Text ($header + $answer) -WithBom | Out-Null

$finishedUtc = Get-UtcStamp
$invocation.finished_utc = $finishedUtc
try { $invocation.duration_seconds = [int]((([datetime]$finishedUtc) - ([datetime]$startedUtc)).TotalSeconds) } catch { }
$invocation['outcome'] = $outcome
$invocation['verdict'] = $verdict
# Its own append-only log, so that "what did we actually run, and how long did it
# take" can be answered without opening every result file.
Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
    event = 'review_invocation'; request_id = $requestId; root_request_id = $rootId
    base_commit = $baseCommit; review_commit = $reviewCommit
    outcome = $outcome; verdict = $verdict
    reviewer = $reviewerKind; review_profile = $reviewProfile; review_effort = $reviewEffort
    duration_seconds = $invocation.duration_seconds
    queued_to_start_seconds = $waitSeconds
    changed_file_count = $invocation.changed_file_count
    changed_lines = $invocation.changed_lines
    diff_bytes = $invocation.diff_bytes
    note = $commandLine
} | Out-Null

$result = Write-ReviewResult -Verdict $verdict -NextAction $nextAction -BlockingCount $blockingCount `
    -ExitCode $exitCode -ReviewerCommand $commandLine -StartedUtc $startedUtc -Notes $notes `
    -LedgerEvent $ledgerEvent -Reviewer $reviewerKind -Outcome $outcome `
    -Invocation ([pscustomobject]$invocation)

# Keep the packet as evidence; the worktree it lived in is about to go.
try {
    Copy-Item -LiteralPath $packetPath -Destination (Join-Path $workDir 'packet.md') -Force
    Copy-Item -LiteralPath $requestPath -Destination (Join-Path $workDir 'request.md') -Force
    Copy-Item -LiteralPath $statPath -Destination (Join-Path $workDir 'diffstat.txt') -Force
    Copy-Item -LiteralPath $commitsPath -Destination (Join-Path $workDir 'commits.txt') -Force
    Copy-Item -LiteralPath $diffPath -Destination (Join-Path $workDir 'diff.patch') -Force
} catch { }
Remove-ReviewWorktree -Path $worktreePath
Say "$requestId reviewed: $verdict -> $($result.next_action)"
if (-not $Quiet) { $result | ConvertTo-Json -Depth 8 }
exit 0
