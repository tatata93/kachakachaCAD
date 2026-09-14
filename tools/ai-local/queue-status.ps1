<#
queue-status.ps1 - what the review queue looks like right now, in one screen.

Read-only. Safe to run at any time, including while the dispatcher is working.
#>

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [switch]$Json,
    [int]$Recent = 10
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')

$RepoRoot = Get-RepoRoot -Hint $RepoRoot
$paths = Get-AiRuntimePaths -RepoRoot $RepoRoot

function Count-Dir {
    param([string]$Dir, [switch]$ResultsOnly)
    $files = @(Get-QueueFiles -Directory $Dir)
    if ($ResultsOnly) {
        # results/ holds both the result and the request it came from; counting
        # both makes every finished review look like two.
        $files = @($files | Where-Object { $_.Name -notlike '*.request.json' })
    }
    return $files.Count
}

# Looking must not change anything. Taking the lock to find out whether it is
# taken can knock over a dispatcher that is starting at that moment.
$lockPath = Join-Path $paths.Locks 'dispatcher.lock'
$dispatcherRunning = Test-SingletonLockHeld -Path $lockPath

$pending = @()
foreach ($f in (Get-QueueFiles -Directory $paths.Incoming)) { $pending += [pscustomobject]@{ state='incoming';   name=$f.Name } }
foreach ($f in (Get-QueueFiles -Directory $paths.Ready))    { $pending += [pscustomobject]@{ state='ready';      name=$f.Name } }
foreach ($f in (Get-QueueFiles -Directory $paths.Processing)) { $pending += [pscustomobject]@{ state='processing'; name=$f.Name } }

$unprocessed = @()
foreach ($f in (Get-QueueFiles -Directory $paths.Results)) {
    if ($f.Name -like '*.request.json') { continue }
    $r = Read-JsonFile -Path $f.FullName
    if ($null -eq $r) { continue }
    $done = $false
    foreach ($p in $r.PSObject.Properties) {
        if ($p.Name -eq 'processed_by_claude' -and [bool]$p.Value) { $done = $true }
    }
    if (-not $done) { $unprocessed += $r }
}

$ledgerTail = @()
$all = @(Get-ReviewLedgerEntries -RepoRoot $RepoRoot)
$start = [Math]::Max(0, $all.Count - $Recent)
for ($i = $start; $i -lt $all.Count; $i++) { $ledgerTail += $all[$i] }

$status = [pscustomobject]@{
    checked_utc        = Get-UtcStamp
    repo_root          = $RepoRoot
    runtime_root       = $paths.Root
    dispatcher_running = $dispatcherRunning
    counts             = [pscustomobject]@{
        incoming   = Count-Dir $paths.Incoming
        ready      = Count-Dir $paths.Ready
        processing = Count-Dir $paths.Processing
        results    = Count-Dir $paths.Results -ResultsOnly
        failed     = Count-Dir $paths.Failed
        stale      = Count-Dir $paths.Stale
    }
    pending            = $pending
    unprocessed_results = @($unprocessed | ForEach-Object {
        $duration = -1
        $profile = ''
        $outcome = 'REVIEWED'
        foreach ($p in $_.PSObject.Properties) {
            if ($p.Name -eq 'outcome') { $outcome = [string]$p.Value }
            if ($p.Name -eq 'review_profile') { $profile = [string]$p.Value }
            if ($p.Name -eq 'invocation' -and $p.Value) {
                foreach ($q in $p.Value.PSObject.Properties) {
                    if ($q.Name -eq 'duration_seconds') { $duration = [int]$q.Value }
                }
            }
        }
        [pscustomobject]@{
            request_id = $_.request_id
            outcome = $outcome
            verdict = $_.verdict
            next_action = $_.next_action
            review_profile = $profile
            duration_seconds = $duration
            review_commit = (Get-ShortSha $_.review_commit)
            finished_utc = $_.finished_utc
        }
    })
    ledger_tail        = $ledgerTail
}

if ($Json) {
    $status | ConvertTo-Json -Depth 8
    exit 0
}

Write-Host ''
Write-Host '  review queue' -ForegroundColor Cyan
Write-Host '  ------------'
Write-Host ("  runtime    : " + $paths.Root)
if ($dispatcherRunning) {
    Write-Host '  dispatcher : running'
} else {
    Write-Host '  dispatcher : NOT running   (start tools\ai-local\start-dispatcher.cmd)' -ForegroundColor Yellow
}
Write-Host ("  incoming {0}   ready {1}   processing {2}   results {3}   failed {4}   stale {5}" -f `
    $status.counts.incoming, $status.counts.ready, $status.counts.processing,
    $status.counts.results, $status.counts.failed, $status.counts.stale)
Write-Host ''
if ($pending.Count -gt 0) {
    Write-Host '  waiting:'
    foreach ($p in $pending) { Write-Host ("    [{0}] {1}" -f $p.state, $p.name) }
    Write-Host ''
}
if ($status.unprocessed_results.Count -gt 0) {
    Write-Host '  reviews Claude has not folded in yet:' -ForegroundColor Yellow
    foreach ($r in $status.unprocessed_results) {
        Write-Host ("    {0}  [{1}] {2} -> {3}  @{4}  {5} {6}s" -f `
            $r.request_id, $r.outcome, $r.verdict, $r.next_action, $r.review_commit,
            $r.review_profile, $r.duration_seconds)
    }
    Write-Host ''
}
if ($ledgerDamage -ne 0) {
    Write-Host ("  the ledger has {0} line(s) that cannot be read (kept in logs\review-ledger.damaged.jsonl)" -f $ledgerDamage) -ForegroundColor Yellow
    Write-Host ''
}
Write-Host '  recent ledger entries:'
foreach ($e in $ledgerTail) {
    $verdict = ''
    foreach ($p in $e.PSObject.Properties) { if ($p.Name -eq 'verdict') { $verdict = ' ' + [string]$p.Value } }
    Write-Host ("    {0}  {1}  {2}{3}" -f $e.logged_utc, $e.event, $e.request_id, $verdict)
}
Write-Host ''
exit 0
