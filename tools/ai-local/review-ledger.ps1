# review-ledger.ps1 - append-only ledger for every review decision.
#
# The ledger is the memory of the pipeline. It answers three questions that the
# rest of the scripts cannot answer on their own:
#   1. has this REQUEST_ID already been reviewed?   (duplicate review guard)
#   2. has this commit already been reviewed?       (repeat-on-restart guard)
#   3. how many blocking verdicts in a row has this root request collected?
#      (three in a row means a human has to look at it)
#
# It is JSON Lines and it is only ever appended to. Nothing in this repository
# rewrites or truncates it; history is evidence.

Set-StrictMode -Version 1.0
. (Join-Path $PSScriptRoot 'review-common.ps1')

function Add-ReviewLedgerEntry {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][hashtable]$Entry
    )
    $paths = Initialize-AiRuntime -RepoRoot $RepoRoot
    if (-not $Entry.ContainsKey('schema_version')) { $Entry['schema_version'] = 1 }
    if (-not $Entry.ContainsKey('logged_utc'))     { $Entry['logged_utc'] = Get-UtcStamp }
    $line = ([pscustomobject]$Entry | ConvertTo-Json -Depth 12 -Compress)
    # A single line, written with a retry: two writers appending at once must not
    # interleave a half line into the evidence file.
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            $stream = [System.IO.File]::Open($paths.Ledger, [System.IO.FileMode]::Append,
                                             [System.IO.FileAccess]::Write,
                                             [System.IO.FileShare]::Read)
            try {
                $bytes = [System.Text.Encoding]::UTF8.GetBytes($line + "`r`n")
                $stream.Write($bytes, 0, $bytes.Length)
                $stream.Flush()
            } finally {
                $stream.Dispose()
            }
            return $true
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    Write-AiLog -Message "ledger write failed for $($Entry['request_id'])" -Level 'ERROR' -LogPath $paths.Dispatcher
    return $false
}

function Get-ReviewLedgerEntries {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$RequestId,
        [string]$RootRequestId,
        [string]$ReviewCommit
    )
    $paths = Get-AiRuntimePaths -RepoRoot $RepoRoot
    if (-not (Test-Path -LiteralPath $paths.Ledger)) { return @() }
    $entries = @()
    foreach ($line in [System.IO.File]::ReadAllLines($paths.Ledger)) {
        if (-not $line -or $line.Trim().Length -eq 0) { continue }
        $obj = $null
        try { $obj = $line | ConvertFrom-Json } catch { continue }
        if ($null -eq $obj) { continue }
        if ($RequestId     -and $obj.request_id      -ne $RequestId)     { continue }
        if ($RootRequestId -and $obj.root_request_id -ne $RootRequestId) { continue }
        if ($ReviewCommit  -and $obj.review_commit   -ne $ReviewCommit)  { continue }
        $entries += $obj
    }
    return $entries
}

# "Already reviewed" means a reviewer actually produced a verdict for this exact
# request id. A dispatch that was started and crashed is not a review.
function Test-AlreadyReviewed {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RequestId
    )
    $entries = Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RequestId $RequestId
    foreach ($e in $entries) {
        if ($e.event -eq 'review_completed') { return $true }
    }
    return $false
}

# The same commit must not be reviewed twice under different request ids either.
function Test-CommitAlreadyReviewed {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RootRequestId,
        [Parameter(Mandatory=$true)][string]$ReviewCommit
    )
    foreach ($e in (Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RootRequestId $RootRequestId -ReviewCommit $ReviewCommit)) {
        if ($e.event -eq 'review_completed') { return $true }
    }
    return $false
}

function Test-AlreadyDispatched {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RequestId
    )
    $entries = Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RequestId $RequestId
    foreach ($e in $entries) {
        if ($e.event -eq 'review_started' -or $e.event -eq 'review_completed') { return $true }
    }
    return $false
}

# Three blocking verdicts in a row on the same root request means the loop is not
# converging. Counting stops at the first non-blocking verdict, so a PASS in the
# middle resets the streak.
function Get-ConsecutiveBlockingCount {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RootRequestId
    )
    $entries = @(Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RootRequestId $RootRequestId |
                 Where-Object { $_.event -eq 'review_completed' })
    if ($entries.Count -eq 0) { return 0 }
    $count = 0
    for ($i = $entries.Count - 1; $i -ge 0; $i--) {
        $action = [string]$entries[$i].next_action
        if ($action -eq 'FIX_AND_REVIEW' -or $action -eq 'STOP' -or $action -eq 'HUMAN_DECISION_REQUIRED') {
            $count++
        } else {
            break
        }
    }
    return $count
}

function Get-LastLedgerEntry {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$RequestId
    )
    $entries = @(Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RequestId $RequestId)
    if ($entries.Count -eq 0) { return $null }
    return $entries[$entries.Count - 1]
}
