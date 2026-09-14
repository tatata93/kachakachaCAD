<#
clear-hold.ps1 - a person says "I have looked at this; carry on".

Three blocking reviews in a row on the same root request stop the queue and hand
the work to a person. That is the point. But a rule that hands work to a person
also has to let that person hand it back, or it is just a dead end.

  tools\ai-local\clear-hold.cmd AI-REVIEW-PIPELINE-JUDGE "looked at it, keep going"

Everything already reviewed stops counting from that moment. Nothing is deleted:
the note is written down, and the reviews themselves stay exactly as they were.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true, Position=0)][string]$RootRequestId,
    [Parameter(Position=1)][string]$Note = '',
    [string]$RepoRoot
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')

$RepoRoot = Get-RepoRoot -Hint $RepoRoot
$paths = Initialize-AiRuntime -RepoRoot $RepoRoot

if (-not (Test-SafeRequestId -RequestId $RootRequestId)) {
    Write-Host "'$RootRequestId' is not a plain request id." -ForegroundColor Red
    exit 2
}

$before = Get-ConsecutiveBlockingCount -RepoRoot $RepoRoot -RootRequestId $RootRequestId
$path = Get-HumanDecisionPath -RepoRoot $RepoRoot -RootRequestId $RootRequestId
Write-JsonAtomic -Path $path -Value ([pscustomobject]@{
    schema_version  = 1
    kind            = 'human_decision'
    root_request_id = $RootRequestId
    cleared_utc     = Get-UtcStamp
    cleared_by      = $env:USERNAME
    machine         = $env:COMPUTERNAME
    note            = $Note
    blocking_streak_at_clearing = $before
}) | Out-Null

Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
    event = 'human_decision'; request_id = ''; root_request_id = $RootRequestId
    next_action = 'PROCEED'
    note = ('a person cleared a streak of ' + $before + ': ' + $Note)
} | Out-Null

$after = Get-ConsecutiveBlockingCount -RepoRoot $RepoRoot -RootRequestId $RootRequestId
Write-Host ("$RootRequestId : blocking streak " + $before + " -> " + $after)
Write-Host ("written to " + $path)
exit 0
