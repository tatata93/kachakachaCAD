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

# Never interrupt a review that is under way. A claim in processing/ means a
# reviewer is running right now; the stale dispatcher can be retired at the next
# build instead. Losing a long review to save a few minutes is a bad trade.
$inFlight = @(Get-QueueFiles -Directory $paths.Processing)
if ($inFlight.Count -gt 0) {
    Say ("a review is under way (" + $inFlight[0].Name + "); no dispatcher is retired now")
    if (-not $Quiet) { Write-Output 'stopped=0 (review in flight)' }
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
    $commandLine = [string]$process.CommandLine
    if (-not $commandLine) { continue }
    if ($commandLine -notlike '*review-dispatcher.ps1*') { continue }
    if ($commandLine -like '*stop-stale-dispatcher*') { continue }

    $startTime = $null
    try { $startTime = (Get-Process -Id $process.ProcessId -ErrorAction Stop).StartTime } catch { $startTime = $null }
    if ($null -eq $startTime) { continue }
    if ($startTime -ge $newest) {
        Say ("dispatcher pid " + $process.ProcessId + " is running current code; left alone")
        continue
    }
    try {
        Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
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
