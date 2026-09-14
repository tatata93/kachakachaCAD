<#
review-enqueue.ps1 - the only door into the review queue.

Called by _FIX_AND_BUILD.cmd after the real build, the real CTest run and the
real self-test have finished on this machine. It turns that evidence into one
review request manifest, or it refuses and says why.

Nothing is enqueued unless the implementer asked for a review. The implementer
asks by committing tools/ai-local/next-review.json, which fixes REQUEST_ID and
BASE. This script fixes HEAD, because only the machine knows which commit it
really built. Codex is therefore never given the job of looking for work.
#>

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [string]$ReviewCommit,      # HEAD as recorded right after the merge, before configure
    [string]$TestedCommit,      # HEAD as recorded after the tests finished
    [string]$BuildResult = 'UNKNOWN',
    [string]$TestResult = 'UNKNOWN',
    [string]$SelfTestResult = 'SKIPPED',
    [string]$Branch = '',
    [string]$CtestEvidence = '',
    [string]$SelfTestEvidence = '',
    [string]$LogPath = '',
    [string]$DeclarationPath = '',
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
    Write-AiLog -Message ("enqueue: " + $Message) -Level $Level -LogPath $paths.Dispatcher -Quiet:$Quiet
}

if (-not $DeclarationPath) {
    $DeclarationPath = Join-Path $RepoRoot 'tools\ai-local\next-review.json'
}
if (-not (Test-Path -LiteralPath $DeclarationPath)) {
    Say "no review was requested for this build (no next-review.json); the reviewer stays asleep"
    Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
        event = 'no_request'; request_id = ''; root_request_id = ''
        review_commit = $ReviewCommit; note = 'no next-review.json in the built tree'
    } | Out-Null
    exit 0
}

$declaration = Read-JsonFile -Path $DeclarationPath
if ($null -eq $declaration) {
    Say "next-review.json is not readable JSON; nothing is enqueued" 'ERROR'
    exit 2
}

function Get-Prop {
    param($Object, [string]$Name, $Default = '')
    foreach ($p in $Object.PSObject.Properties) {
        if ($p.Name -eq $Name) { return $p.Value }
    }
    return $Default
}

$requestId = [string](Get-Prop $declaration 'request_id')
$baseCommit = [string](Get-Prop $declaration 'base_commit')
if (-not $requestId -or -not $baseCommit) {
    Say "next-review.json must name request_id and base_commit; nothing is enqueued" 'ERROR'
    exit 2
}

if (-not $ReviewCommit) { $ReviewCommit = Get-HeadCommit -RepoRoot $RepoRoot -GitExe $gitExe }
if (-not $TestedCommit) { $TestedCommit = Get-HeadCommit -RepoRoot $RepoRoot -GitExe $gitExe }
if (-not $Branch)       { $Branch = Get-CurrentBranch -RepoRoot $RepoRoot -GitExe $gitExe }

# Refuse loudly rather than enqueue a request that would have to be thrown away.
$refusals = @()
if ($BuildResult -ne 'PASS') { $refusals += "build did not pass (build_result=$BuildResult)" }
if ($TestResult  -ne 'PASS') { $refusals += "tests did not pass (test_result=$TestResult)" }
if ($SelfTestResult -ne 'PASS' -and $SelfTestResult -ne 'SKIPPED') {
    $refusals += "the application self-test did not pass (selftest_result=$SelfTestResult)"
}
if ($ReviewCommit -ne $TestedCommit) {
    $refusals += ("the branch moved while this build ran: " +
                  (Get-ShortSha $ReviewCommit) + " -> " + (Get-ShortSha $TestedCommit))
}

$existing = Get-ReviewLedgerEntries -RepoRoot $RepoRoot -RequestId $requestId
$alreadyEnqueued = $false
foreach ($e in $existing) { if ($e.event -eq 'request_enqueued') { $alreadyEnqueued = $true } }
if ($alreadyEnqueued) {
    Say "$requestId was already enqueued once; a new review needs a new REQUEST_ID (R5 -> R6)" 'WARN'
    Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
        event = 'duplicate_request'; request_id = $requestId
        root_request_id = (Get-RootRequestId -RequestId $requestId)
        review_commit = $ReviewCommit; note = 'request id already used'
    } | Out-Null
    exit 0
}

if ($refusals.Count -gt 0) {
    $reason = ($refusals -join '; ')
    Say "$requestId is not sent to the reviewer: $reason" 'WARN'
    $failedPath = Join-Path $paths.Failed ($requestId + '.json')
    Write-JsonAtomic -Path $failedPath -Value ([pscustomobject]@{
        schema_version = 1
        kind           = 'review_request_refused'
        request_id     = $requestId
        root_request_id = (Get-RootRequestId -RequestId $requestId)
        created_utc    = Get-UtcStamp
        base_commit    = $baseCommit
        review_commit  = $ReviewCommit
        tested_commit  = $TestedCommit
        build_result   = $BuildResult
        test_result    = $TestResult
        selftest_result = $SelfTestResult
        reason         = $reason
        next_owner     = 'claude'
    }) | Out-Null
    Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
        event = 'request_refused'; request_id = $requestId
        root_request_id = (Get-RootRequestId -RequestId $requestId)
        base_commit = $baseCommit; review_commit = $ReviewCommit; tested_commit = $TestedCommit
        build_result = $BuildResult; test_result = $TestResult; selftest_result = $SelfTestResult
        next_action = 'FIX_AND_REVIEW'; note = $reason
    } | Out-Null
    exit 1
}

$manifest = [ordered]@{
    schema_version  = 1
    kind            = 'review_request'
    request_id      = $requestId
    root_request_id = (Get-RootRequestId -RequestId $requestId)
    attempt         = (Get-RequestAttempt -RequestId $requestId)
    created_utc     = Get-UtcStamp
    producer        = '_FIX_AND_BUILD.cmd'
    repo_path       = $RepoRoot
    base_commit     = $baseCommit
    review_commit   = $ReviewCommit
    tested_commit   = $TestedCommit
    branch_at_test  = $Branch
    build_result    = $BuildResult
    test_result     = $TestResult
    selftest_result = $SelfTestResult
    evidence        = [ordered]@{
        ctest    = $CtestEvidence
        selftest = $SelfTestEvidence
        log      = $LogPath
    }
    review_effort   = [string](Get-Prop $declaration 'review_effort' 'MEDIUM')
    scope_ja        = [string](Get-Prop $declaration 'scope_ja' '')
    focus           = @(Get-Prop $declaration 'focus' @())
    policy          = [string](Get-Prop $declaration 'policy' 'docs/ai/CODEX_REVIEW_POLICY.md')
}

$target = Join-Path $paths.Incoming ($requestId + '.json')
Write-JsonAtomic -Path $target -Value ([pscustomobject]$manifest) | Out-Null
Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
    event = 'request_enqueued'; request_id = $requestId
    root_request_id = $manifest.root_request_id
    base_commit = $baseCommit; review_commit = $ReviewCommit; tested_commit = $TestedCommit
    build_result = $BuildResult; test_result = $TestResult; selftest_result = $SelfTestResult
    note = 'build and tests passed on this machine'
} | Out-Null
Say "$requestId queued for review at $(Get-ShortSha $ReviewCommit) (base $(Get-ShortSha $baseCommit))"
if (-not $Quiet) { Write-Output $target }
exit 0
