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
    [int]$TimeoutSeconds = 3600,
    [switch]$Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')

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
$requestId = [string]$manifest.request_id
$rootId = Get-RootRequestId -RequestId $requestId
$reviewCommit = [string]$manifest.review_commit
$baseCommit = [string]$manifest.base_commit

if ($DryRun.IsPresent -eq $false -and (Test-AlreadyReviewed -RepoRoot $RepoRoot -RequestId $requestId)) {
    Say "$requestId already has a review recorded; refusing to review it twice" 'WARN'
    exit 3
}

$workDir = Join-Path $paths.Processing $requestId
if (-not (Test-Path -LiteralPath $workDir)) {
    New-Item -ItemType Directory -Path $workDir -Force | Out-Null
}
$packetPath   = Join-Path $workDir 'packet.md'
$diffPath     = Join-Path $workDir 'diff.patch'
$statPath     = Join-Path $workDir 'diffstat.txt'
$commitsPath  = Join-Path $workDir 'commits.txt'
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
    # Only ever the throwaway review worktree. The implementer's checkout and its
    # uncommitted work are never touched by this script.
    if ($Path -notlike (Join-Path $worktreeRoot '*')) { return }
    $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'remove', '--force', $Path)
    if ($r.ExitCode -ne 0 -and (Test-Path -LiteralPath $Path)) {
        try { Remove-Item -LiteralPath $Path -Recurse -Force } catch { }
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
        [string]$LedgerEvent = 'review_completed'
    )
    $finished = Get-UtcStamp
    $streak = Get-ConsecutiveBlockingCount -RepoRoot $RepoRoot -RootRequestId $rootId
    $effectiveNext = $NextAction
    if ($NextAction -ne 'PROCEED' -and ($streak + 1) -ge 3) {
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
        verdict              = $Verdict
        next_action          = $effectiveNext
        blocking_count       = $BlockingCount
        consecutive_blocking = ($streak + 1)
        exit_code            = $ExitCode
        review_text_file     = $resultText
        packet_file          = $packetPath
        worktree             = $worktreePath
        notes                = @($Notes)
        processed_by_claude  = $false
    }
    if ($Verdict -eq 'PASS') { $result['consecutive_blocking'] = 0 }
    Write-JsonAtomic -Path $resultJson -Value ([pscustomobject]$result) | Out-Null
    # An infrastructure failure is not a verdict. Only a real review closes a
    # REQUEST_ID; otherwise the same id could never be tried again.
    Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
        event = $LedgerEvent; request_id = $requestId; root_request_id = $rootId
        base_commit = $baseCommit; review_commit = $reviewCommit
        verdict = $Verdict; next_action = $effectiveNext
        blocking_count = $BlockingCount; reviewer = $Reviewer; exit_code = $ExitCode
        note = (@($Notes) -join '; ')
    } | Out-Null
    return $result
}

# ---------------------------------------------------------------- packet ----

$commits = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @(
    'log', '--no-color', '--oneline', ($baseCommit + '..' + $reviewCommit))
Write-TextAtomic -Path $commitsPath -Text $commits.StdOut | Out-Null

$stat = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @(
    'diff', '--no-color', '--stat', $baseCommit, $reviewCommit)
Write-TextAtomic -Path $statPath -Text $stat.StdOut | Out-Null

$diff = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @(
    'diff', '--no-color', $baseCommit, $reviewCommit)
Write-TextAtomic -Path $diffPath -Text $diff.StdOut | Out-Null

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

## Files in this packet

- commits.txt  : commit list BASE..HEAD
- diffstat.txt : changed files
- diff.patch   : the complete fixed diff BASE..HEAD

The working tree you are in is a detached checkout pinned to HEAD. It will be
deleted after the review. Do not edit it.

The rules for your answer are in docs/ai/CODEX_REVIEW_POLICY.md in this tree.
"@
Write-TextAtomic -Path $packetPath -Text $packet | Out-Null

# ------------------------------------------------------------- reviewer ----

$interface = Read-JsonFile -Path $paths.Interface
if ($null -eq $interface -or -not $interface.probe_ok) {
    # Probe once here rather than assume anything about the installed reviewer.
    & (Join-Path $PSScriptRoot 'review-precheck.ps1') -RepoRoot $RepoRoot -Machine -Refresh -Quiet | Out-Null
    $interface = Read-JsonFile -Path $paths.Interface
}
if ($null -eq $interface -or -not $interface.probe_ok) {
    Say "no usable reviewer command on this machine; nothing is started" 'ERROR'
    Write-TextAtomic -Path $resultText -Text "reviewer unavailable on this machine" | Out-Null
    Write-ReviewResult -Verdict 'ERROR' -NextAction 'HUMAN_DECISION_REQUIRED' -BlockingCount 0 `
        -ExitCode 127 -ReviewerCommand '' -StartedUtc (Get-UtcStamp) `
        -Notes @('no codex executable was found or it does not support "codex exec"') -Reviewer 'none' `
        -LedgerEvent 'review_unavailable' | Out-Null
    exit 4
}

$flags = @()
foreach ($f in @($interface.supported_flags)) { $flags += [string]$f }

$prompt = "Read docs/ai/CODEX_REVIEW_POLICY.md in this working tree, then review the packet at $packetPath. " +
          "REQUEST_ID is $requestId. BASE is $baseCommit and HEAD is $reviewCommit; review exactly that range and nothing else. " +
          "This is a read-only review: do not modify any file. " +
          "Answer in the exact block the policy describes."

$arguments = @('exec')
if ($flags -contains '-c') {
    $effortMap = @{ 'LOW' = 'low'; 'MEDIUM' = 'medium'; 'HIGH' = 'high'; 'EXTRA_HIGH' = 'xhigh' }
    $effortKey = [string]$manifest.review_effort
    if ($effortMap.ContainsKey($effortKey)) {
        $arguments += @('-c', ('model_reasoning_effort="' + $effortMap[$effortKey] + '"'))
    }
}
if ($flags -contains '--ephemeral')   { $arguments += '--ephemeral' }
if ($flags -contains '--sandbox')     { $arguments += @('--sandbox', 'read-only') }
if ($flags -contains '--output-last-message') { $arguments += @('--output-last-message', $lastMsgPath) }
if ($flags -contains '-C')            { $arguments += @('-C', $worktreePath) }
elseif ($flags -contains '--cd')      { $arguments += @('--cd', $worktreePath) }
$arguments += $prompt

$commandLine = ('{0} {1}' -f $interface.executable, (ConvertTo-CommandLine -Arguments $arguments))

if ($DryRun) {
    Say "DRY_RUN for $requestId; the reviewer is not started"
    Write-TextAtomic -Path (Join-Path $workDir 'dry-run-command.txt') -Text $commandLine | Out-Null
    Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
        event = 'dry_run'; request_id = $requestId; root_request_id = $rootId
        base_commit = $baseCommit; review_commit = $reviewCommit
        note = $commandLine
    } | Out-Null
    Write-Output $commandLine
    exit 0
}

# A detached worktree at a fixed commit. The branch may move under us during the
# review; this checkout cannot.
Remove-ReviewWorktree -Path $worktreePath
$add = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @(
    'worktree', 'add', '--detach', $worktreePath, $reviewCommit)
if ($add.ExitCode -ne 0) {
    Say ("could not create the review worktree: " + $add.StdErr.Trim()) 'ERROR'
    Write-TextAtomic -Path $resultText -Text ("review worktree could not be created:`n" + $add.StdErr) | Out-Null
    Write-ReviewResult -Verdict 'ERROR' -NextAction 'HUMAN_DECISION_REQUIRED' -BlockingCount 0 `
        -ExitCode $add.ExitCode -ReviewerCommand $commandLine -StartedUtc (Get-UtcStamp) `
        -Notes @('git worktree add failed') -LedgerEvent 'review_unavailable' | Out-Null
    exit 5
}

$startedUtc = Get-UtcStamp
Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
    event = 'review_started'; request_id = $requestId; root_request_id = $rootId
    base_commit = $baseCommit; review_commit = $reviewCommit
    reviewer = 'codex'; note = $commandLine
} | Out-Null
Say "starting the reviewer once for $requestId at $(Get-ShortSha $reviewCommit)"

$run = $null
$notes = @()
try {
    $run = Invoke-Process -FilePath $interface.executable -Arguments $arguments `
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
    $answer = [System.IO.File]::ReadAllText($lastMsgPath)
}
if (-not $answer -or $answer.Trim().Length -eq 0) { $answer = $stdoutText }

# The reviewer is read-only. If the pinned checkout came back dirty, that is
# reported; the change is dropped from the throwaway worktree only.
$dirty = Invoke-Git -RepoRoot $worktreePath -GitExe $gitExe -Arguments @('status', '--porcelain')
if ($dirty.ExitCode -eq 0 -and $dirty.StdOut.Trim().Length -gt 0) {
    $notes += 'the reviewer modified files; a review is read-only, so the edits were dropped'
    Write-TextAtomic -Path (Join-Path $workDir 'reviewer-touched-files.txt') -Text $dirty.StdOut | Out-Null
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
if ($blockingText -match '^\d+$') { $blockingCount = [int]$blockingText }

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
if (-not $verdict) {
    $verdict = 'ERROR'
    $notes += 'the reviewer did not answer in the required form'
}
if (-not $nextAction) {
    switch ($verdict) {
        'PASS'     { $nextAction = 'PROCEED' }
        'BLOCKING' { $nextAction = 'FIX_AND_REVIEW' }
        'STOP'     { $nextAction = 'STOP' }
        default    { $nextAction = 'HUMAN_DECISION_REQUIRED' }
    }
}
if ($verdict -eq 'BLOCKING' -and $blockingCount -eq 0) { $blockingCount = 1 }

$header = @"
## $requestId

REQUEST_ID: $requestId
BASE: $baseCommit
HEAD: $reviewCommit
REVIEWER: codex ($($interface.version))
STARTED: $startedUtc
VERDICT: $verdict
NEXT_ACTION: $nextAction

"@
Write-TextAtomic -Path $resultText -Text ($header + $answer) | Out-Null

$result = Write-ReviewResult -Verdict $verdict -NextAction $nextAction -BlockingCount $blockingCount `
    -ExitCode $exitCode -ReviewerCommand $commandLine -StartedUtc $startedUtc -Notes $notes

Remove-ReviewWorktree -Path $worktreePath
Say "$requestId reviewed: $verdict -> $($result.next_action)"
if (-not $Quiet) { $result | ConvertTo-Json -Depth 8 }
exit 0
