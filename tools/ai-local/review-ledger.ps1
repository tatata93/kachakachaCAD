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
        [Parameter(Mandatory=$true)][hashtable]$Entry,
        # A line that MUST reach the ledger. If it cannot, the caller is stopped
        # rather than carrying on: a missing review_completed would let the same
        # REQUEST_ID be used again and overwrite a result that already exists.
        [switch]$Required
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
                # If the last writer died mid-line, this file does not end with a
                # newline. Appending straight onto it would glue the two together
                # into one broken line, and from then on the whole ledger reads as
                # damaged and every request is refused. Close the old line first.
                $prefix = ''
                if ($stream.Length -gt 0) {
                    $probe = [System.IO.File]::Open($paths.Ledger, [System.IO.FileMode]::Open,
                                                    [System.IO.FileAccess]::Read,
                                                    [System.IO.FileShare]::ReadWrite)
                    try {
                        $probe.Seek(-1, [System.IO.SeekOrigin]::End) | Out-Null
                        $last = $probe.ReadByte()
                        if ($last -ne 10 -and $last -ne 13) { $prefix = "`r`n" }
                    } finally { $probe.Dispose() }
                }
                $bytes = [System.Text.Encoding]::UTF8.GetBytes($prefix + $line + "`r`n")
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
    if ($Required) {
        throw ("the ledger could not be written for " + [string]$Entry['request_id'] +
               " (" + [string]$Entry['event'] + "); stopping rather than losing the record")
    }
    return $false
}

# How many lines in the ledger could not be read back. A ledger that cannot be
# read in full cannot answer "has this been reviewed already", so the answer must
# not be guessed from what survived.
function Get-ReviewLedgerDamage {
    param([Parameter(Mandatory=$true)][string]$RepoRoot)
    $paths = Get-AiRuntimePaths -RepoRoot $RepoRoot
    if (-not (Test-Path -LiteralPath $paths.Ledger)) { return 0 }
    $lines = @(Read-LedgerLines -Path $paths.Ledger)
    $damaged = 0
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if (-not $line -or $line.Trim().Length -eq 0) { continue }
        # The very last line may be half written at this instant. That is a write
        # in progress, not damage, and calling it damage would refuse good work.
        if ($index -eq ($lines.Count - 1)) { continue }
        try { $null = $line | ConvertFrom-Json } catch { $damaged++ }
    }
    return $damaged
}

# Reading while someone is appending is normal, not damage. The writer holds the
# file with FileShare.Read, so a read can still collide with the moment it opens;
# a sharing violation is retried rather than reported as a broken ledger, and an
# unfinished last line is simply not there yet.
function Read-LedgerLines {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                             [System.IO.FileAccess]::Read,
                                             [System.IO.FileShare]::ReadWrite)
            try {
                $reader = New-Object System.IO.StreamReader($stream, (New-Object System.Text.UTF8Encoding($false)))
                $text = $reader.ReadToEnd()
            } finally { $stream.Dispose() }
            return @($text -split "`r?`n")
        } catch {
            Start-Sleep -Milliseconds 50
        }
    }
    throw ("the ledger at " + $Path + " could not be read")
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
    foreach ($line in (Read-LedgerLines -Path $paths.Ledger)) {
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
# converging. Counting stops at the first verdict that is not BLOCKING, so a PASS
# in the middle resets the streak.
#
# STOP is not counted here at all. STOP already means "a person has to decide",
# once, immediately. Folding it into a three-strike count would make it weaker
# than it is.
#
# A person can clear a streak with clear-hold.cmd. Without that, a rule meant to
# hand work to a human would be a dead end instead.
function Get-HumanDecisionPath {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RootRequestId
    )
    $paths = Get-AiRuntimePaths -RepoRoot $RepoRoot
    return (Join-Path (Join-Path $paths.Root 'human-decisions') ($RootRequestId + '.cleared.json'))
}

function Get-HumanClearedUtc {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RootRequestId
    )
    $note = Read-JsonFile -Path (Get-HumanDecisionPath -RepoRoot $RepoRoot -RootRequestId $RootRequestId)
    if ($null -eq $note) { return $null }
    foreach ($p in $note.PSObject.Properties) {
        if ($p.Name -eq 'cleared_utc' -and $p.Value) {
            try { return [datetime]$p.Value } catch { return $null }
        }
    }
    return $null
}

function Get-ConsecutiveBlockingCount {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$RootRequestId
    )
    $entries = @(Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RootRequestId $RootRequestId |
                 Where-Object { $_.event -eq 'review_completed' })
    if ($entries.Count -eq 0) { return 0 }
    $clearedAt = Get-HumanClearedUtc -RepoRoot $RepoRoot -RootRequestId $RootRequestId
    $count = 0
    for ($i = $entries.Count - 1; $i -ge 0; $i--) {
        if ($clearedAt) {
            $loggedAt = $null
            try { $loggedAt = [datetime]$entries[$i].logged_utc } catch { $loggedAt = $null }
            # Everything a person has already looked at stops counting.
            if ($loggedAt -and $loggedAt -le $clearedAt) { break }
        }
        if ([string]$entries[$i].next_action -eq 'FIX_AND_REVIEW') { $count++ } else { break }
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
