<#
stop-stale-dispatcher.ps1 - retire a resident dispatcher that is older than its code.

A dispatcher keeps running whatever it loaded at startup. When a newer version of
the review scripts arrives, the resident is out of date. New dispatchers stand
down on their own; an older one predates that behaviour, so this retires it.

Only a dispatcher that started BEFORE the newest script file is stopped. One that
is running current code is left alone, review in flight and all. A stopped
dispatcher's claim is recovered by review-recover.ps1 at the next startup, so no
request is lost.
#>

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [switch]$Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')

$RepoRoot = Get-RepoRoot -Hint $RepoRoot
$paths = Initialize-AiRuntime -RepoRoot $RepoRoot

function Say {
    param([string]$Message, [string]$Level = 'INFO')
    Write-AiLog -Message ("stop-stale: " + $Message) -Level $Level -LogPath $paths.Dispatcher -Quiet:$Quiet
}

$newest = [datetime]'2000-01-01'
foreach ($file in @(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Name -like '*.ps1' })) {
    if ($file.LastWriteTime -gt $newest) { $newest = $file.LastWriteTime }
}

# Never interrupt a review that is really under way. But "a claim exists" is not
# the same as "work is happening": a dispatcher that hung cannot be rescued by
# waiting for it, and it would hold the queue for ever. A claim that has outlived
# its own time limit by a wide margin counts as stuck, not as busy.
$inFlight = @(Get-QueueFiles -Directory $paths.Processing)
$busy = @()
$stuck = @()
foreach ($claim in $inFlight) {
    $manifest = Read-JsonFile -Path $claim.FullName
    $budget = 1200
    if ($manifest) {
        foreach ($p in $manifest.PSObject.Properties) {
            if ($p.Name -eq 'timeout_seconds' -and $p.Value) { $budget = [int]$p.Value }
        }
    }
    $ownerPath = Join-Path $paths.Processing ($claim.Name + '.owner')
    $claimed = $claim.LastWriteTime
    $haveClaimTime = $false
    $owner = Read-JsonFile -Path $ownerPath
    if ($owner) {
        foreach ($p in $owner.PSObject.Properties) {
            if ($p.Name -eq 'claimed_utc' -and $p.Value) {
                try { $claimed = ([datetime]$p.Value).ToLocalTime(); $haveClaimTime = $true } catch { }
            }
        }
    }
    $age = ((Get-Date) - $claimed).TotalSeconds
    # No owner file yet means the claim was made moments ago and the dispatcher has
    # not finished writing it down. That is a busy dispatcher, not a stuck one.
    if (-not $haveClaimTime -and $age -lt 300) { $busy += $claim; continue }
    # The time limit, plus ten minutes for the reviewer to be shut down and
    # written up. Past that, nothing is coming.
    if ($age -gt ($budget + 600)) { $stuck += $claim } else { $busy += $claim }
}
if ($busy.Count -gt 0) {
    Say ("a review is under way (" + $busy[0].Name + "); no dispatcher is retired now")
    if (-not $Quiet) { Write-Output 'stopped=0 (review in flight)' }
    exit 0
}
if ($stuck.Count -gt 0) {
    Say ("a claim has outlived its own time limit (" + $stuck[0].Name +
         "); the dispatcher holding it is stuck and will be retired") 'WARN'
}

# Only this runtime's dispatcher is ours to retire. Matching on the command line
# alone would also catch a healthy dispatcher serving another checkout on the same
# machine, and stopping that one would be someone else's outage.
$lockPath = Join-Path $paths.Locks 'dispatcher.lock'
$lockOwner = Get-SingletonLockOwner -Path $lockPath
if ($lockOwner -and $lockOwner.repo_root) {
    # An owner file left behind by another checkout is not ours to act on.
    try {
        $ownerRoot = [System.IO.Path]::GetFullPath($lockOwner.repo_root).TrimEnd('\', '/')
        $thisRoot = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
        if ($ownerRoot.ToLowerInvariant() -ne $thisRoot.ToLowerInvariant()) {
            Say ("the lock here is owned by " + $ownerRoot + "; nothing is stopped") 'WARN'
            if (-not $Quiet) { Write-Output 'stopped=0 (another checkout)' }
            exit 0
        }
    } catch { }
}
if ($null -eq $lockOwner -or $lockOwner.pid -le 0) {
    Say "no dispatcher is recorded as holding this runtime's lock; nothing is stopped"
    if (-not $Quiet) { Write-Output 'stopped=0 (no owner recorded)' }
    exit 0
}

$stopped = 0
$processes = @()
try {
    $processes = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" -ErrorAction Stop)
} catch {
    Say "cannot look at the process list on this machine; nothing is stopped" 'WARN'
    exit 0
}

foreach ($process in $processes) {
    if ($process.ProcessId -eq $PID) { continue }
    # The lock says which process owns THIS runtime. Nothing else is touched.
    if ([int]$process.ProcessId -ne [int]$lockOwner.pid) { continue }
    $commandLine = [string]$process.CommandLine
    if (-not $commandLine) { continue }
    if ($commandLine -notlike '*review-dispatcher.ps1*') {
        Say ("pid " + $process.ProcessId + " holds the lock but is not a dispatcher; left alone") 'WARN'
        continue
    }
    if ($commandLine -like '*stop-stale-dispatcher*') { continue }

    $startTime = $null
    try { $startTime = (Get-Process -Id $process.ProcessId -ErrorAction Stop).StartTime } catch { $startTime = $null }
    if ($null -eq $startTime) { continue }
    # A stuck dispatcher is retired whatever code it is running: waiting longer
    # cannot help it, and nothing else can free the queue.
    if ($stuck.Count -eq 0 -and $startTime -ge $newest) {
        Say ("dispatcher pid " + $process.ProcessId + " is running current code; left alone")
        continue
    }
    try {
        # The tree, not just the shell: a hung dispatcher usually has a reviewer
        # still running underneath it.
        Stop-ProcessTree -ProcessId ([int]$process.ProcessId)
        try {
            $ownerPath = Get-SingletonLockOwnerPath -Path $lockPath
            if (Test-Path -LiteralPath $ownerPath) { Remove-Item -LiteralPath $ownerPath -Force }
        } catch { }
        $stopped++
        Say ("retired dispatcher pid " + $process.ProcessId + " (started " + $startTime.ToString('s') +
             ", scripts changed " + $newest.ToString('s') + ")")
    } catch {
        Say ("could not stop dispatcher pid " + $process.ProcessId + ": " + $_.Exception.Message) 'WARN'
    }
}

if ($stopped -gt 0) {
    # A retired dispatcher may have been holding a claim. Put it back now rather
    # than waiting for the next startup scan.
    & (Join-Path $PSScriptRoot 'review-recover.ps1') -RepoRoot $RepoRoot -Quiet | Out-Null
}
if (-not $Quiet) { Write-Output ("stopped=" + $stopped) }
exit 0
