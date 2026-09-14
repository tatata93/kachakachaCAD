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
. (Join-Path $PSScriptRoot 'review-profile.ps1')

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

# One build can carry more than one review request: several REQUEST_IDs may share
# the same HEAD. next-review.json is therefore either one declaration or
# { "kind": "review_request_declarations", "requests": [ ... ] }.
$declarations = @()
$requestsProperty = Get-Prop $declaration 'requests' $null
if ($null -ne $requestsProperty) {
    foreach ($item in @($requestsProperty)) { $declarations += $item }
} else {
    $declarations += $declaration
}

# A declaration may split itself into segments. Each segment becomes its own
# request over its own paths, so a wide change is read in pieces that make sense
# instead of being handed over whole. The segment name goes before the revision
# suffix, so P1-EXTRUDE-R5 with segment QUEUE becomes P1-EXTRUDE-QUEUE-R5 and
# keeps its own history.
function Expand-Segments {
    param($Declaration)
    $segments = Get-Prop $Declaration 'segments' $null
    if ($null -eq $segments) { return @($Declaration) }
    $expanded = @()
    foreach ($segment in @($segments)) {
        $name = [string](Get-Prop $segment 'name' '')
        if (-not $name) { continue }
        $baseId = [string](Get-Prop $Declaration 'request_id' '')
        $id = $baseId + '-' + $name
        if ($baseId -match '^(?<head>.+)-R(?<n>\d+)$') {
            $id = $Matches['head'] + '-' + $name + '-R' + $Matches['n']
        }
        $copy = [pscustomobject]@{
            request_id    = $id
            base_commit   = [string](Get-Prop $Declaration 'base_commit' '')
            review_profile = [string](Get-Prop $segment 'profile' (Get-Prop $Declaration 'review_profile' ''))
            force_profile = [bool](Get-Prop $segment 'force_profile' (Get-Prop $Declaration 'force_profile' $false))
            scope_ja      = [string](Get-Prop $segment 'scope_ja' (Get-Prop $Declaration 'scope_ja' ''))
            focus         = @(Get-Prop $segment 'focus' (Get-Prop $Declaration 'focus' @()))
            paths         = @(Get-Prop $segment 'paths' @())
            policy        = [string](Get-Prop $Declaration 'policy' 'docs/ai/CODEX_REVIEW_POLICY.md')
            allow_large   = [bool](Get-Prop $segment 'allow_large' $false)
        }
        $expanded += $copy
    }
    if ($expanded.Count -eq 0) { return @($Declaration) }
    return $expanded
}

$flattened = @()
foreach ($item in $declarations) {
    foreach ($piece in (Expand-Segments -Declaration $item)) { $flattened += $piece }
}
$declarations = $flattened
if ($declarations.Count -eq 0) {
    Say "next-review.json declares no request; nothing is enqueued" 'WARN'
    exit 0
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

function Add-OneRequest {
    param($Declaration)

    $requestId = [string](Get-Prop $Declaration 'request_id')
    $baseCommit = [string](Get-Prop $Declaration 'base_commit')
    if (-not $requestId -or -not $baseCommit) {
        Say "a declaration without request_id or base_commit is ignored" 'ERROR'
        return 2
    }
    # The id becomes a file name and a folder name. Refuse anything else here, at
    # the only door, rather than discovering it three scripts later.
    if (-not (Test-SafeRequestId -RequestId $requestId)) {
        Say ("'" + $requestId + "' is not a plain name; a request id is used as a file and folder name") 'ERROR'
        return 2
    }
    # A moving name such as HEAD or main is not a fixed review target. Resolve it
    # now, so that what is written down cannot mean something else later.
    $resolvedBase = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('rev-parse', ($baseCommit + '^{commit}'))
    if ($resolvedBase.ExitCode -ne 0) {
        Say ("base_commit '" + $baseCommit + "' is not a commit in this repository") 'ERROR'
        return 2
    }
    $baseCommit = $resolvedBase.StdOut.Trim()

    # A REQUEST_ID is used up by a real review, not by an attempt. If the reviewer
    # was simply unavailable, the same id may be queued again; if it was reviewed,
    # the fix needs a new id (R5 -> R6) so the old verdict keeps standing.
    $blockReason = ''
    if (Test-AlreadyReviewed -RepoRoot $RepoRoot -RequestId $requestId) {
        $blockReason = "$requestId already has a review; a fix needs a new REQUEST_ID (R5 -> R6)"
    } else {
        foreach ($dir in @($paths.Incoming, $paths.Ready, $paths.Processing)) {
            if (Test-Path -LiteralPath (Join-Path $dir ($requestId + '.json'))) {
                $blockReason = "$requestId is already waiting in the queue"
            }
        }
    }
    if ($blockReason) {
        Say $blockReason 'WARN'
        Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
            event = 'duplicate_request'; request_id = $requestId
            root_request_id = (Get-RootRequestId -RequestId $requestId)
            review_commit = $ReviewCommit; note = $blockReason
        } | Out-Null
        return 0
    }

    if ($refusals.Count -gt 0) {
        $reason = ($refusals -join '; ')
        Say "$requestId is not sent to the reviewer: $reason" 'WARN'
        $failedPath = Join-Path $paths.Failed ($requestId + '.json')
        Write-JsonAtomic -Path $failedPath -Value ([pscustomobject]@{
            schema_version  = 1
            kind            = 'review_request_refused'
            request_id      = $requestId
            root_request_id = (Get-RootRequestId -RequestId $requestId)
            created_utc     = Get-UtcStamp
            base_commit     = $baseCommit
            review_commit   = $ReviewCommit
            tested_commit   = $TestedCommit
            build_result    = $BuildResult
            test_result     = $TestResult
            selftest_result = $SelfTestResult
            reason          = $reason
            next_owner      = 'claude'
        }) | Out-Null
        Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
            event = 'request_refused'; request_id = $requestId
            root_request_id = (Get-RootRequestId -RequestId $requestId)
            base_commit = $baseCommit; review_commit = $ReviewCommit; tested_commit = $TestedCommit
            build_result = $BuildResult; test_result = $TestResult; selftest_result = $SelfTestResult
            next_action = 'FIX_AND_REVIEW'; note = $reason
        } | Out-Null
        return 1
    }

    # What actually changed, measured now, so that the profile and the size guard
    # are decided from the change itself rather than from what someone called it.
    $pathFilter = @()
    foreach ($item in @(Get-Prop $Declaration 'paths' @())) { if ($item) { $pathFilter += [string]$item } }
    $pathArguments = @()
    if ($pathFilter.Count -gt 0) { $pathArguments = @('--') + $pathFilter }

    $names = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments (@(
        'diff', '--no-color', '--name-only', $baseCommit, $ReviewCommit) + $pathArguments)
    $changedFiles = @()
    foreach ($line in ($names.StdOut -split "`r?`n")) {
        if ($line.Trim().Length -gt 0) { $changedFiles += $line.Trim() }
    }
    $diff = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments (@(
        'diff', '--no-color', $baseCommit, $ReviewCommit) + $pathArguments)
    $diffText = $diff.StdOut

    $profile = Resolve-ReviewProfile -Declared ([string](Get-Prop $Declaration 'review_profile' (Get-Prop $Declaration 'review_effort' ''))) `
        -ChangedFiles $changedFiles -DiffText $diffText `
        -ForceDeclared ([bool](Get-Prop $Declaration 'force_profile' $false))

    # A change too wide to read in one sitting is not handed over whole. The
    # implementer declares segments that make sense; the machine will not guess a
    # split, because guessing one can cut an atomic change in half.
    $maxFiles = 40
    $maxLines = 2500
    if ($env:KACHA_MAX_REVIEW_FILES) { $maxFiles = [int]$env:KACHA_MAX_REVIEW_FILES }
    if ($env:KACHA_MAX_REVIEW_LINES) { $maxLines = [int]$env:KACHA_MAX_REVIEW_LINES }
    $allowLarge = [bool](Get-Prop $Declaration 'allow_large' $false)
    if (-not $allowLarge -and (($changedFiles.Count -gt $maxFiles) -or ($profile.changed_lines -gt $maxLines))) {
        $groups = @()
        foreach ($file in $changedFiles) {
            $top = (($file -replace '\\', '/') -split '/')[0]
            if ($groups -notcontains $top) { $groups += $top }
        }
        $reason = ("AIR-E050 this change is too wide for one review (" + $changedFiles.Count +
                   " files, " + $profile.changed_lines + " changed lines; limits are " +
                   $maxFiles + " and " + $maxLines + "). Declare segments in next-review.json " +
                   "so it is read in pieces that make sense, or set allow_large when the change " +
                   "really must be judged whole. Top level groups here: " + ($groups -join ', '))
        Say $reason 'WARN'
        Write-JsonAtomic -Path (Join-Path $paths.Failed ($requestId + '.json')) -Value ([pscustomobject]@{
            schema_version = 1; kind = 'review_request_refused'; request_id = $requestId
            root_request_id = (Get-RootRequestId -RequestId $requestId)
            created_utc = Get-UtcStamp; base_commit = $baseCommit
            review_commit = $ReviewCommit; tested_commit = $TestedCommit
            changed_files = $changedFiles.Count; changed_lines = $profile.changed_lines
            reason = $reason; next_owner = 'claude'
        }) | Out-Null
        Add-ReviewLedgerEntry -RepoRoot $RepoRoot -Entry @{
            event = 'request_too_large'; request_id = $requestId
            root_request_id = (Get-RootRequestId -RequestId $requestId)
            base_commit = $baseCommit; review_commit = $ReviewCommit
            next_action = 'FIX_AND_REVIEW'; note = $reason
        } | Out-Null
        return 1
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
        review_profile  = $profile.profile
        review_effort   = $profile.effort
        profile_reason  = $profile.reason
        profile_declared = $profile.declared
        profile_measured = $profile.measured
        risk_signals    = @($profile.risk_signals)
        changed_files   = @($changedFiles)
        changed_file_count = $changedFiles.Count
        changed_lines   = $profile.changed_lines
        diff_bytes      = $diffText.Length
        timeout_seconds = $profile.timeout_seconds
        scope_ja        = [string](Get-Prop $Declaration 'scope_ja' '')
        focus           = @(Get-Prop $Declaration 'focus' @())
        paths           = $pathFilter
        policy          = [string](Get-Prop $Declaration 'policy' 'docs/ai/CODEX_REVIEW_POLICY.md')
    }

    $target = Join-Path $paths.Incoming ($requestId + '.json')
    Write-JsonAtomic -Path $target -Value ([pscustomobject]$manifest) | Out-Null
    # Without this line the same id could be queued twice.
    Add-ReviewLedgerEntry -Required -RepoRoot $RepoRoot -Entry @{
        event = 'request_enqueued'; request_id = $requestId
        root_request_id = $manifest.root_request_id
        base_commit = $baseCommit; review_commit = $ReviewCommit; tested_commit = $TestedCommit
        build_result = $BuildResult; test_result = $TestResult; selftest_result = $SelfTestResult
        review_profile = $profile.profile; review_effort = $profile.effort
        changed_file_count = $changedFiles.Count; changed_lines = $profile.changed_lines
        note = 'build and tests passed on this machine; ' + $profile.reason
    } | Out-Null
    # Only the status code leaves this function; anything written to the output
    # stream would be returned alongside it and break the caller's comparison.
    Say ("$requestId queued at " + (Get-ShortSha $ReviewCommit) + " (base " + (Get-ShortSha $baseCommit) +
         ") profile=" + $profile.profile + " effort=" + $profile.effort +
         " files=" + $changedFiles.Count + " lines=" + $profile.changed_lines +
         " because " + $profile.reason)
    return 0
}

$worst = 0
foreach ($item in $declarations) {
    $rc = Add-OneRequest -Declaration $item
    if ($rc -gt $worst) { $worst = $rc }
}
exit $worst
