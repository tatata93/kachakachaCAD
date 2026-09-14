<#
review-dispatcher.ps1 - the ordinary program that watches the queue.

This is the only thing that polls, and it is not an AI. It runs once as a
background window on the machine and does four things forever:

  startup  : recover anything a crash left half-done
  validate : incoming -> ready (or failed, with the reason written down)
  claim    : ready -> processing, atomically, one owner per request
  run      : review-runner.ps1 once, then scan again

A FileSystemWatcher makes it react immediately, but it is never trusted on its
own: every cycle also scans the directories, and there is a scan at startup and
another after each completed review. A missed event costs a few seconds, not a
lost review.

Usage
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\ai-local\review-dispatcher.ps1
  ... -Once      one pass and exit (used by the self-test and by scripts)
  ... -DryRun    do everything except start the reviewer
#>

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [switch]$Once,
    [switch]$DryRun,
    [int]$IntervalSeconds = 15,
    [int]$TimeoutSeconds = 3600,
    [switch]$Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')

$RepoRoot = Get-RepoRoot -Hint $RepoRoot
$paths = Initialize-AiRuntime -RepoRoot $RepoRoot

function Say {
    param([string]$Message, [string]$Level = 'INFO')
    Write-AiLog -Message ("dispatcher: " + $Message) -Level $Level -LogPath $paths.Dispatcher -Quiet:$Quiet
}

# -------------------------------------------------------------- singleton ---
# Two dispatchers would fight over the same requests. The second one says so and
# leaves instead of starting a duplicate review.
$lockPath = Join-Path $paths.Locks 'dispatcher.lock'
$lockStream = New-SingletonLock -Path $lockPath
if ($null -eq $lockStream) {
    Say "another dispatcher already owns $lockPath; this one exits" 'WARN'
    exit 10
}

$script:Processed = 0
$script:Validated = 0
$script:Refused = 0

function Invoke-ValidatePass {
    foreach ($file in (Get-QueueFiles -Directory $paths.Incoming)) {
        $name = $file.Name
        # One precheck, and its JSON answer is the decision. The exit code alone
        # would not tell the queue why a request was refused.
        $answerText = ''
        try {
            $answerText = (& (Join-Path $PSScriptRoot 'review-precheck.ps1') `
                -RepoRoot $RepoRoot -ManifestPath $file.FullName | Out-String)
        } catch {
            $answerText = ''
        }
        $parsed = $null
        if ($answerText -and $answerText.Trim().Length -gt 0) {
            try { $parsed = $answerText | ConvertFrom-Json } catch { $parsed = $null }
        }
        $ok = $false
        $message = 'the precheck gave no answer'
        if ($parsed) {
            $ok = [bool]$parsed.ok
            $message = [string]$parsed.message
        }
        if ($ok) {
            $target = Join-Path $paths.Ready $name
            if (Move-QueueItemAtomic -Source $file.FullName -Destination $target) {
                $script:Validated++
                Say "accepted $name"
            }
        } else {
            $target = Join-Path $paths.Failed $name
            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
            if (Move-QueueItemAtomic -Source $file.FullName -Destination $target) {
                $script:Refused++
                Say "refused $name : $message" 'WARN'
                $manifest = Read-JsonFile -Path $target
                $requestId = ''
                if ($manifest) { $requestId = [string]$manifest.request_id }
                Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
                    event = 'request_rejected'; request_id = $requestId
                    root_request_id = (Get-RootRequestId -RequestId $requestId)
                    next_action = 'FIX_AND_REVIEW'; note = $message
                } | Out-Null
            }
        }
    }
}

function Invoke-ClaimAndRunPass {
    foreach ($file in (Get-QueueFiles -Directory $paths.Ready)) {
        $name = $file.Name
        $claimPath = Join-Path $paths.Processing $name
        # The atomic claim. Whoever wins the move owns the request.
        if (-not (Move-QueueItemAtomic -Source $file.FullName -Destination $claimPath)) {
            Say "$name is already claimed by someone else" 'WARN'
            continue
        }
        $manifest = Read-JsonFile -Path $claimPath
        $requestId = ''
        if ($manifest) { $requestId = [string]$manifest.request_id }
        $ownerPath = Join-Path $paths.Processing ($name + '.owner')
        Write-JsonAtomic -Path $ownerPath -Value ([pscustomobject]@{
            schema_version = 1
            kind = 'review_claim'
            request_id = $requestId
            pid = $PID
            machine = $env:COMPUTERNAME
            claimed_utc = Get-UtcStamp
        }) | Out-Null

        $exitCode = 0
        try {
            & (Join-Path $PSScriptRoot 'review-runner.ps1') -RepoRoot $RepoRoot `
                -ManifestPath $claimPath -TimeoutSeconds $TimeoutSeconds -DryRun:$DryRun -Quiet | Out-Null
            $exitCode = $LASTEXITCODE
        } catch {
            $exitCode = 99
            Say ("the runner threw for " + $name + ": " + $_.Exception.Message) 'ERROR'
        }

        $done = Join-Path $paths.Results ($name -replace '\.json$', '.request.json')
        if ($exitCode -eq 0) {
            if (Test-Path -LiteralPath $done) { Remove-Item -LiteralPath $done -Force }
            Move-QueueItemAtomic -Source $claimPath -Destination $done | Out-Null
        } else {
            $failed = Join-Path $paths.Failed $name
            if (Test-Path -LiteralPath $failed) { Remove-Item -LiteralPath $failed -Force }
            Move-QueueItemAtomic -Source $claimPath -Destination $failed | Out-Null
            Say "$name did not complete (exit $exitCode)" 'WARN'
        }
        if (Test-Path -LiteralPath $ownerPath) { Remove-Item -LiteralPath $ownerPath -Force }
        $script:Processed++
        # A scan after each review, because work may have arrived while we were busy.
        Invoke-ValidatePass
    }
}

function Invoke-Pass {
    Invoke-ValidatePass
    Invoke-ClaimAndRunPass
}

try {
    Say "startup recovery scan"
    & (Join-Path $PSScriptRoot 'review-recover.ps1') -RepoRoot $RepoRoot -Quiet | Out-Null

    if ($Once) {
        Invoke-Pass
        Say ("one pass done: validated=$script:Validated refused=$script:Refused reviewed=$script:Processed")
        exit 0
    }

    $watcher = New-Object System.IO.FileSystemWatcher
    $watcher.Path = $paths.Incoming
    $watcher.Filter = '*.json'
    $watcher.IncludeSubdirectories = $false
    $watcher.EnableRaisingEvents = $true
    Say "watching $($paths.Incoming) (a full scan also runs every $IntervalSeconds seconds)"

    while ($true) {
        Invoke-Pass
        # WaitForChanged returns early on an event and otherwise times out; either
        # way the next thing that happens is a full scan, so a lost event is
        # only a delay.
        try {
            $watcher.WaitForChanged([System.IO.WatcherChangeTypes]::All, $IntervalSeconds * 1000) | Out-Null
        } catch {
            Start-Sleep -Seconds $IntervalSeconds
        }
    }
} finally {
    if ($lockStream) {
        try { $lockStream.Dispose() } catch { }
    }
}
