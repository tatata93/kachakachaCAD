<#
review-recover.ps1 - put the queue back into a state a dispatcher can trust.

Runs at dispatcher startup, and can be run by hand. It looks for the three ways
a crash leaves rubbish behind:

  1. a claim in processing/ whose owner process is gone
       -> the review never finished; requeue it, or park it in stale/ if a
          result for it already exists.
  2. a half-written *.tmp file
       -> a producer died mid-write; the file was never visible to a consumer
          and is removed.
  3. a review worktree with no matching claim
       -> left over from a killed runner; the worktree is removed.

Nothing here deletes a result, a ledger line or anything under the implementer's
own checkout.
#>

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [int]$StaleMinutes = 240,
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
    Write-AiLog -Message ("recover: " + $Message) -Level $Level -LogPath $paths.Dispatcher -Quiet:$Quiet
}

$requeued = 0
$parked = 0
$removedTmp = 0
$removedWorktrees = 0

# 1. abandoned claims -------------------------------------------------------
foreach ($file in (Get-QueueFiles -Directory $paths.Processing)) {
    $name = $file.Name
    $ownerPath = Join-Path $paths.Processing ($name + '.owner')
    $owner = Read-JsonFile -Path $ownerPath
    $ownerAlive = $false
    if ($owner -and $owner.pid) {
        # Identity, not just a number: the pid together with the moment that
        # process started, on this machine, working in this checkout. Windows
        # reuses process ids, and stopping a stranger's process tree because it
        # inherited a number would be unforgivable.
        $ownerStart = ''
        $ownerRepo = ''
        foreach ($p in $owner.PSObject.Properties) {
            if ($p.Name -eq 'process_started_utc') { $ownerStart = [string]$p.Value }
            if ($p.Name -eq 'repo_root') { $ownerRepo = [string]$p.Value }
        }
        $ownerAlive = Test-ProcessAlive -ProcessId ([int]$owner.pid) -StartedUtc $ownerStart
        if ($ownerAlive -and $owner.machine -and $env:COMPUTERNAME -and
            $owner.machine -ne $env:COMPUTERNAME) {
            $ownerAlive = $false
        }
        if ($ownerAlive -and $ownerRepo) {
            try {
                $a = [System.IO.Path]::GetFullPath($ownerRepo).TrimEnd('\', '/').ToLowerInvariant()
                $b = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/').ToLowerInvariant()
                if ($a -ne $b) { $ownerAlive = $false }
            } catch { $ownerAlive = $false }
        }
        if (-not $ownerAlive -and $ownerStart) {
            # Not our process after all. Requeue the claim, but never stop anything.
            $stuck = $false
        }
    }
    $manifest = Read-JsonFile -Path $file.FullName
    $requestId = ''
    $budget = 1200

    if ($manifest) {
        $requestId = [string]$manifest.request_id
        foreach ($p in $manifest.PSObject.Properties) {
            if ($p.Name -eq 'timeout_seconds' -and $p.Value) { $budget = [int]$p.Value }
        }
    }
    # How old the claim is, measured from when it was CLAIMED. The owner file says
    # so; the claim file's own write time is only a fallback, and between the move
    # and the owner file being written there is a moment where neither is reliable,
    # so a fresh-looking claim with no owner yet is given room rather than killed.
    $claimedAt = $file.LastWriteTime
    $haveClaimTime = $false
    if ($owner) {
        foreach ($p in $owner.PSObject.Properties) {
            if ($p.Name -eq 'claimed_utc' -and $p.Value) {
                try { $claimedAt = ([datetime]$p.Value).ToLocalTime(); $haveClaimTime = $true } catch { }
            }
        }
    }
    $ageSeconds = ((Get-Date) - $claimedAt).TotalSeconds
    if (-not $haveClaimTime -and $ageSeconds -lt 300) { continue }
    # A live owner is not proof of progress. Past its own time limit plus ten
    # minutes, a claim is stuck, and waiting the default four hours helps nobody.
    $stuck = ($ageSeconds -gt ($budget + 600))
    # A live owner is left alone unless it is past its own limit. Requeueing a
    # claim whose reviewer is still running would have two reviewers on the same
    # request at once; an old wall-clock rule is not a reason to risk that.
    if ($ownerAlive -and -not $stuck) { continue }
    if ($ownerAlive -and $stuck) {
        Say ("$($file.Name) has been claimed for " + [int]$ageSeconds + "s with a limit of " +
             $budget + "s; the process holding it is stopped") 'WARN'
        if ($owner -and $owner.pid) { Stop-ProcessTree -ProcessId ([int]$owner.pid) }
        Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
            event = 'review_timeout'; request_id = $requestId
            root_request_id = (Get-RootRequestId -RequestId $requestId)
            outcome = 'TIMEOUT'; next_action = 'RETRY'
            note = ('the claim outlived its limit of ' + $budget + 's; this is not a review result')
        } | Out-Null
    }

    $resultPath = Join-Path $paths.Results ($requestId + '.json')
    $hasResult = ($requestId -and (Test-Path -LiteralPath $resultPath))
    if ($hasResult -or ($requestId -and (Test-AlreadyReviewed -RepoRoot $RepoRoot -RequestId $requestId))) {
        # The review did finish; only the bookkeeping was interrupted.
        $target = Join-Path $paths.Results (($name -replace '\.json$', '') + '.request.json')
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        if (Move-QueueItemAtomic -Source $file.FullName -Destination $target) {
            Say "$name was already reviewed; its claim is closed"
        }
    } else {
        $target = Join-Path $paths.Ready $name
        if (Test-Path -LiteralPath $target) {
            $target = Join-Path $paths.Stale $name
            $parked++
        } else {
            $requeued++
        }
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        if (Move-QueueItemAtomic -Source $file.FullName -Destination $target) {
            Say "$name was left claimed by a process that is gone; it goes back to $(Split-Path -Leaf (Split-Path -Parent $target))"
            Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
                event = 'claim_recovered'; request_id = $requestId
                root_request_id = (Get-RootRequestId -RequestId $requestId)
                note = 'the dispatcher that claimed this request is no longer running'
            } | Out-Null
        }
    }
    if (Test-Path -LiteralPath $ownerPath) { Remove-Item -LiteralPath $ownerPath -Force }
}

# 2. half-written files -----------------------------------------------------
foreach ($dir in @($paths.Incoming, $paths.Ready, $paths.Processing, $paths.Results, $paths.Failed, $paths.Stale)) {
    if (-not (Test-Path -LiteralPath $dir)) { continue }
    foreach ($tmp in @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -like '*.tmp' })) {
        try { Remove-Item -LiteralPath $tmp.FullName -Force; $removedTmp++ } catch { }
    }
}

# 3. orphan review worktrees ------------------------------------------------
function Get-WorktreeRoot {
    if ($env:KACHA_AI_WORKTREE_ROOT) { return $env:KACHA_AI_WORKTREE_ROOT }
    $sibling = Join-Path (Split-Path -Parent $RepoRoot) 'kachakachaCAD-worktrees'
    if (Test-Path -LiteralPath $sibling) { return (Join-Path $sibling 'ai-review') }
    return $paths.Worktrees
}
$worktreeRoot = Get-WorktreeRoot
if (Test-Path -LiteralPath $worktreeRoot) {
    # The worktree root can be shared with another checkout. A directory sitting in
    # it is not ours simply because we have no claim for it, so only worktrees that
    # THIS repository has registered are touched, and a directory git refuses to
    # remove is never deleted by hand.
    $listed = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'list', '--porcelain')
    $ours = @{}
    foreach ($line in ($listed.StdOut -split "`r?`n")) {
        if ($line -notlike 'worktree *') { continue }
        $candidate = $line.Substring(9).Trim()
        try { $candidate = [System.IO.Path]::GetFullPath($candidate).TrimEnd('\', '/').ToLowerInvariant() } catch { continue }
        $ours[$candidate] = $true
    }
    foreach ($dir in @(Get-ChildItem -LiteralPath $worktreeRoot -Directory)) {
        $claim = Join-Path $paths.Processing ($dir.Name + '.json')
        if (Test-Path -LiteralPath $claim) { continue }
        $full = ''
        try { $full = [System.IO.Path]::GetFullPath($dir.FullName).TrimEnd('\', '/').ToLowerInvariant() } catch { continue }
        if (-not $ours.ContainsKey($full)) {
            Say "$($dir.Name) is not a worktree of this repository; left alone"
            continue
        }
        $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'remove', '--force', $dir.FullName)
        if ($r.ExitCode -ne 0 -and (Test-Path -LiteralPath $dir.FullName)) {
            Say "git would not remove $($dir.Name) ($($r.StdErr.Trim())); left alone" 'WARN'
            continue
        }
        $removedWorktrees++
        Say "removed the leftover review worktree $($dir.Name)"
    }
    Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('worktree', 'prune') | Out-Null
}

$summary = [pscustomobject]@{
    requeued          = $requeued
    parked            = $parked
    removed_tmp       = $removedTmp
    removed_worktrees = $removedWorktrees
}
Say ("recovery done: requeued=$requeued parked=$parked tmp=$removedTmp worktrees=$removedWorktrees")
if (-not $Quiet) { $summary | ConvertTo-Json -Depth 4 }
exit 0
