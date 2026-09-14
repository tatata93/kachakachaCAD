<#
review-selftest.ps1 - prove the review pipeline behaves, on a throwaway repository.

Every case below is one of the promises the owner asked for:

  1  no request            -> the reviewer is never started
  2  build failed          -> not enqueued, sent back to the implementer
  3  tests failed          -> not enqueued, sent back to the implementer
  4  TESTED != REVIEW      -> not enqueued
  5  build and tests pass  -> the reviewer starts exactly once
  6  run the queue again   -> the same request is not reviewed twice
  7  a duplicate REQUEST_ID-> refused at the door
  8  HEAD moves mid-review -> the reviewer still sees the pinned commit
  9  crash then restart    -> the claim is recovered and the review happens
  10 half-written file     -> never read, and cleaned up
  11 three blockings       -> a person is asked to decide
  12 the reviewer edits    -> reported, and the edit is dropped
  13 no reviewer installed -> reported, and the REQUEST_ID is not used up
  14 two requests, one HEAD-> both reviewed, once each

It creates its own git repository under the temp directory, uses a stub reviewer,
and touches nothing in the real checkout.
#>

[CmdletBinding()]
param(
    [string]$WorkRoot,
    [switch]$KeepWorkRoot
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')

$Tools = $PSScriptRoot
$script:Passed = 0
$script:Failed = 0
$script:Failures = @()

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    if ($Condition) {
        $script:Passed++
        Write-Host ("PASS " + $Name) -ForegroundColor Green
    } else {
        $script:Failed++
        $script:Failures += ($Name + ' : ' + $Detail)
        Write-Host ("FAIL " + $Name + '  ' + $Detail) -ForegroundColor Red
    }
}

if (-not $WorkRoot) {
    $WorkRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('kacha-review-selftest-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
}
if (Test-Path -LiteralPath $WorkRoot) { Remove-Item -LiteralPath $WorkRoot -Recurse -Force }
New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null

$repo = Join-Path $WorkRoot 'repo'
$runtime = Join-Path $WorkRoot 'runtime'
$worktreeRoot = Join-Path $WorkRoot 'worktrees'
$stubLog = Join-Path $WorkRoot 'stub-calls.txt'
$stubCmd = Join-Path $WorkRoot 'codex-stub.cmd'
$stubPs1 = Join-Path $WorkRoot 'codex-stub.ps1'

New-Item -ItemType Directory -Path $repo -Force | Out-Null
New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null

$gitExe = Resolve-GitExe
function Git {
    param([string[]]$Arguments, [string]$At = $repo)
    $r = Invoke-Process -FilePath $gitExe -Arguments $Arguments -WorkingDirectory $At
    if ($r.ExitCode -ne 0) { throw ("git " + ($Arguments -join ' ') + " failed: " + $r.StdErr) }
    return $r.StdOut
}

Git @('init', '-q', '-b', 'work')
Git @('config', 'user.email', 'selftest@example.invalid')
Git @('config', 'user.name', 'review selftest')
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'one' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'base commit')
$baseCommit = (Git @('rev-parse', 'HEAD')).Trim()
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'two' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'work commit')
$headCommit = (Git @('rev-parse', 'HEAD')).Trim()

# --- the stub reviewer ------------------------------------------------------
# It behaves like the real thing in the only ways that matter here: it answers
# "codex exec --help" with a flag list, and when run it writes its answer to the
# file named by --output-last-message.
@'
param()
$ErrorActionPreference = "Stop"
$argsList = @($args)
Add-Content -LiteralPath $env:KACHA_STUB_LOG -Value ((Get-Location).Path + " -- " + ($argsList -join " "))
if ($argsList -contains "--version") { Write-Output "codex-stub 0.0.0"; exit 0 }
if ($argsList.Count -ge 2 -and $argsList[0] -eq "exec" -and $argsList -contains "--help") {
    Write-Output "Usage: codex exec [OPTIONS] [PROMPT]"
    Write-Output "  -C, --cd <DIR>                 working directory"
    Write-Output "  -c, --config <KEY=VALUE>       override a config value"
    Write-Output "      --sandbox <MODE>           read-only | workspace-write"
    Write-Output "      --ephemeral                do not keep a session"
    Write-Output "      --output-last-message <F>  write the final message to a file"
    exit 0
}
$outFile = ""
for ($i = 0; $i -lt $argsList.Count - 1; $i++) {
    if ($argsList[$i] -eq "--output-last-message") { $outFile = $argsList[$i + 1] }
}
if ($env:KACHA_STUB_WRITE_FILE) {
    Set-Content -LiteralPath (Join-Path (Get-Location).Path $env:KACHA_STUB_WRITE_FILE) -Value "the reviewer should not do this" -Encoding ASCII
}
$verdict = $env:KACHA_STUB_VERDICT
if (-not $verdict) { $verdict = "PASS" }
$next = "PROCEED"
$blocking = 0
if ($verdict -ne "PASS") { $next = "FIX_AND_REVIEW"; $blocking = 1 }
$gitForStub = $env:KACHA_GIT_EXE
if (-not $gitForStub) { $gitForStub = "git" }
$head = (& $gitForStub rev-parse HEAD) 2>$null
$answer = @"
VERDICT: $verdict
NEXT_ACTION: $next
BLOCKING_COUNT: $blocking
REVIEWED_WORKTREE_HEAD: $head

stub review body
"@
if ($outFile) { Set-Content -LiteralPath $outFile -Value $answer -Encoding UTF8 }
Write-Output $answer
exit 0
'@ | Set-Content -LiteralPath $stubPs1 -Encoding UTF8

@"
@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "$stubPs1" %*
"@ | Set-Content -LiteralPath $stubCmd -Encoding ASCII

$env:KACHA_AI_RUNTIME = $runtime
$env:KACHA_AI_WORKTREE_ROOT = $worktreeRoot
$env:KACHA_CODEX_EXE = $stubCmd
$env:KACHA_GIT_EXE = $gitExe
$env:KACHA_STUB_LOG = $stubLog
$env:KACHA_STUB_VERDICT = 'PASS'
$env:KACHA_STUB_WRITE_FILE = ''
Set-Content -LiteralPath $stubLog -Value '' -Encoding ASCII

$paths = Initialize-AiRuntime -RepoRoot $repo

function Stub-CallCount {
    if (-not (Test-Path -LiteralPath $stubLog)) { return 0 }
    $lines = @([System.IO.File]::ReadAllLines($stubLog) | Where-Object { $_.Trim().Length -gt 0 })
    # --version and "exec --help" probes are not reviews.
    $reviews = @($lines | Where-Object { $_ -notlike '*--help*' -and $_ -notlike '*--version*' })
    return $reviews.Count
}

function Write-Declaration {
    param([string]$RequestId, [string]$Base)
    Write-JsonAtomic -Path (Join-Path $repo 'next-review.json') -Value ([pscustomobject]@{
        schema_version = 1
        kind = 'review_request_declaration'
        request_id = $RequestId
        base_commit = $Base
        review_effort = 'MEDIUM'
        scope_ja = 'self test'
        focus = @('self test')
        policy = 'docs/ai/CODEX_REVIEW_POLICY.md'
    }) | Out-Null
    return (Join-Path $repo 'next-review.json')
}

function Enqueue {
    param(
        [string]$RequestId, [string]$Base, [string]$Review, [string]$Tested,
        [string]$Build = 'PASS', [string]$Test = 'PASS', [string]$SelfTest = 'PASS'
    )
    $decl = Write-Declaration -RequestId $RequestId -Base $Base
    & (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $decl `
        -ReviewCommit $Review -TestedCommit $Tested -BuildResult $Build -TestResult $Test `
        -SelfTestResult $SelfTest -Branch 'work' -Quiet | Out-Null
    return $LASTEXITCODE
}

function Run-Dispatcher {
    & (Join-Path $Tools 'review-dispatcher.ps1') -RepoRoot $repo -Once -Quiet | Out-Null
    return $LASTEXITCODE
}

# 1 -------------------------------------------------------------------------
Run-Dispatcher | Out-Null
Check 'no request means the reviewer is never started' ((Stub-CallCount) -eq 0) ("calls=" + (Stub-CallCount))

# 2, 3, 4 -------------------------------------------------------------------
Enqueue -RequestId 'T-BUILDFAIL-R1' -Base $baseCommit -Review $headCommit -Tested $headCommit -Build 'FAIL' | Out-Null
Check 'a failed build is not enqueued' ((@(Get-QueueFiles -Directory $paths.Incoming)).Count -eq 0) 'incoming should be empty'
Check 'a failed build is written down as refused' (Test-Path -LiteralPath (Join-Path $paths.Failed 'T-BUILDFAIL-R1.json')) 'no failed record'

Enqueue -RequestId 'T-TESTFAIL-R1' -Base $baseCommit -Review $headCommit -Tested $headCommit -Test 'FAIL' | Out-Null
Check 'failed tests are not enqueued' (-not (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-TESTFAIL-R1.json'))) 'it reached incoming'

Enqueue -RequestId 'T-MOVED-R1' -Base $baseCommit -Review $baseCommit -Tested $headCommit | Out-Null
Check 'a branch that moved during the build is not enqueued' (-not (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-MOVED-R1.json'))) 'it reached incoming'

Run-Dispatcher | Out-Null
Check 'none of the refused requests started a reviewer' ((Stub-CallCount) -eq 0) ("calls=" + (Stub-CallCount))

# 5 -------------------------------------------------------------------------
Enqueue -RequestId 'T-OK-R1' -Base $baseCommit -Review $headCommit -Tested $headCommit | Out-Null
Check 'a passing build reaches the queue' (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-OK-R1.json')) 'not in incoming'
Run-Dispatcher | Out-Null
Check 'a passing build starts the reviewer exactly once' ((Stub-CallCount) -eq 1) ("calls=" + (Stub-CallCount))
$result1 = Read-JsonFile -Path (Join-Path $paths.Results 'T-OK-R1.json')
Check 'the verdict is recorded' (($null -ne $result1) -and $result1.verdict -eq 'PASS') 'no PASS result'
Check 'the recorded review commit is the tested commit' (($null -ne $result1) -and $result1.review_commit -eq $headCommit) 'wrong review commit'

# 6 -------------------------------------------------------------------------
Run-Dispatcher | Out-Null
Check 'the same request is not reviewed twice' ((Stub-CallCount) -eq 1) ("calls=" + (Stub-CallCount))

# 7 -------------------------------------------------------------------------
$rc = Enqueue -RequestId 'T-OK-R1' -Base $baseCommit -Review $headCommit -Tested $headCommit
Check 'a reused REQUEST_ID is refused at the door' (-not (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-OK-R1.json'))) 'it was enqueued again'

# 8 -------------------------------------------------------------------------
Enqueue -RequestId 'T-PIN-R1' -Base $baseCommit -Review $headCommit -Tested $headCommit | Out-Null
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'three' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'a commit made while the review is queued')
$movedHead = (Git @('rev-parse', 'HEAD')).Trim()
Run-Dispatcher | Out-Null
$pinText = ''
if (Test-Path -LiteralPath (Join-Path $paths.Results 'T-PIN-R1.md')) {
    $pinText = [System.IO.File]::ReadAllText((Join-Path $paths.Results 'T-PIN-R1.md'))
}
Check 'the branch really did move' ($movedHead -ne $headCommit) 'the test did not move HEAD'
Check 'the reviewer saw the pinned commit, not the new tip' ($pinText -like ('*REVIEWED_WORKTREE_HEAD: ' + $headCommit + '*')) 'the reviewer saw a different commit'

# 9 -------------------------------------------------------------------------
Enqueue -RequestId 'T-CRASH-R1' -Base $baseCommit -Review $movedHead -Tested $movedHead | Out-Null
& (Join-Path $Tools 'review-dispatcher.ps1') -RepoRoot $repo -Once -DryRun -Quiet | Out-Null
# Simulate a dispatcher that died mid-review: put the claim back with a dead owner.
$crashManifest = Join-Path $paths.Results 'T-CRASH-R1.request.json'
Check 'the dry run did not start a reviewer' ((Stub-CallCount) -eq 2) ("calls=" + (Stub-CallCount))
if (Test-Path -LiteralPath $crashManifest) {
    Move-Item -LiteralPath $crashManifest -Destination (Join-Path $paths.Processing 'T-CRASH-R1.json') -Force
}
Write-JsonAtomic -Path (Join-Path $paths.Processing 'T-CRASH-R1.json.owner') -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_claim'; request_id = 'T-CRASH-R1'
    pid = 999999; machine = $env:COMPUTERNAME; claimed_utc = (Get-UtcStamp)
}) | Out-Null
& (Join-Path $Tools 'review-recover.ps1') -RepoRoot $repo -Quiet | Out-Null
Check 'a claim whose owner is gone goes back to the queue' (Test-Path -LiteralPath (Join-Path $paths.Ready 'T-CRASH-R1.json')) 'it was not requeued'
Run-Dispatcher | Out-Null
Check 'the recovered request is reviewed after the restart' (Test-Path -LiteralPath (Join-Path $paths.Results 'T-CRASH-R1.json')) 'no result'

# 10 ------------------------------------------------------------------------
$before = Stub-CallCount
Set-Content -LiteralPath (Join-Path $paths.Incoming 'T-PARTIAL-R1.json.tmp') -Value '{ "schema_ver' -Encoding ASCII
Run-Dispatcher | Out-Null
Check 'a half-written request is never read' ((Stub-CallCount) -eq $before) ("calls=" + (Stub-CallCount))
Check 'a half-written request is cleaned up' (-not (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-PARTIAL-R1.json.tmp'))) 'the tmp file is still there'

# 11 ------------------------------------------------------------------------
$env:KACHA_STUB_VERDICT = 'BLOCKING'
foreach ($n in @(1, 2, 3)) {
    Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value ("blocking-" + $n) -Encoding ASCII
    Git @('add', '-A'); Git @('commit', '-q', '-m', ("blocking round " + $n))
    $h = (Git @('rev-parse', 'HEAD')).Trim()
    Enqueue -RequestId ('T-LOOP-R' + $n) -Base $baseCommit -Review $h -Tested $h | Out-Null
    Run-Dispatcher | Out-Null
}
$third = Read-JsonFile -Path (Join-Path $paths.Results 'T-LOOP-R3.json')
Check 'three blocking reviews in a row ask a person to decide' `
    (($null -ne $third) -and $third.next_action -eq 'HUMAN_DECISION_REQUIRED') `
    ("next_action=" + $(if ($third) { $third.next_action } else { 'none' }))

# 12 ------------------------------------------------------------------------
$env:KACHA_STUB_VERDICT = 'PASS'
$env:KACHA_STUB_WRITE_FILE = 'reviewer-was-here.txt'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'readonly-case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'read only case')
$roHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-RO-R1' -Base $baseCommit -Review $roHead -Tested $roHead | Out-Null
Run-Dispatcher | Out-Null
$roResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-RO-R1.json')
$noteText = ''
if ($roResult) { $noteText = (@($roResult.notes) -join ' ') }
Check 'a reviewer that edits files is reported' ($noteText -like '*read-only*') ("notes=" + $noteText)
Check 'the edit did not survive into the repository' (-not (Test-Path -LiteralPath (Join-Path $repo 'reviewer-was-here.txt'))) 'the file reached the repository'
$env:KACHA_STUB_WRITE_FILE = ''

# 13 ------------------------------------------------------------------------
# An installation problem must not consume a REQUEST_ID. Only a real review does.
$goodStub = $env:KACHA_CODEX_EXE
$env:KACHA_CODEX_EXE = (Join-Path $WorkRoot 'no-such-reviewer.cmd')
Remove-Item -LiteralPath $paths.Interface -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'no reviewer case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'no reviewer case')
$noHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-NOCODEX-R1' -Base $baseCommit -Review $noHead -Tested $noHead | Out-Null
Run-Dispatcher | Out-Null
$noResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-NOCODEX-R1.json')
Check 'a missing reviewer is reported, not hidden' `
    (($null -ne $noResult) -and $noResult.verdict -eq 'ERROR') 'no ERROR result'
Check 'a missing reviewer does not count as a review' `
    (-not (Test-AlreadyReviewed -RepoRoot $repo -RequestId 'T-NOCODEX-R1')) 'it was counted as reviewed'

$env:KACHA_CODEX_EXE = $goodStub
Remove-Item -LiteralPath $paths.Interface -Force -ErrorAction SilentlyContinue
Enqueue -RequestId 'T-NOCODEX-R1' -Base $baseCommit -Review $noHead -Tested $noHead | Out-Null
Check 'the same request may be queued again once the reviewer is back' `
    (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-NOCODEX-R1.json')) 'it was refused'
Run-Dispatcher | Out-Null
$retry = Read-JsonFile -Path (Join-Path $paths.Results 'T-NOCODEX-R1.json')
Check 'the retry is reviewed for real' (($null -ne $retry) -and $retry.verdict -eq 'PASS') 'no PASS on retry'

# 14 ------------------------------------------------------------------------
# One build can carry several REQUEST_IDs that share the same HEAD.
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'two requests case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'two requests case')
$twoHead = (Git @('rev-parse', 'HEAD')).Trim()
$manyPath = Join-Path $repo 'next-review-many.json'
Write-JsonAtomic -Path $manyPath -Value ([pscustomobject]@{
    schema_version = 1
    kind = 'review_request_declarations'
    requests = @(
        [pscustomobject]@{ request_id = 'T-MANY-A-R1'; base_commit = $baseCommit; review_effort = 'LOW'; scope_ja = 'a' },
        [pscustomobject]@{ request_id = 'T-MANY-B-R1'; base_commit = $baseCommit; review_effort = 'LOW'; scope_ja = 'b' }
    )
}) | Out-Null
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $manyPath `
    -ReviewCommit $twoHead -TestedCommit $twoHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
$beforeMany = Stub-CallCount
Check 'both requests reach the queue' `
    ((Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-MANY-A-R1.json')) -and
     (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-MANY-B-R1.json'))) 'one of them is missing'
Run-Dispatcher | Out-Null
Check 'both requests are reviewed, once each' ((Stub-CallCount) -eq ($beforeMany + 2)) ("calls=" + (Stub-CallCount))
Check 'the first of the pair has a result' (Test-Path -LiteralPath (Join-Path $paths.Results 'T-MANY-A-R1.json')) 'no result for A'
Check 'the second of the pair has a result' (Test-Path -LiteralPath (Join-Path $paths.Results 'T-MANY-B-R1.json')) 'no result for B'

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host ("review pipeline self-test: {0} passed, {1} failed, {2} total" -f $script:Passed, $script:Failed, ($script:Passed + $script:Failed))
foreach ($f in $script:Failures) { Write-Host ("  - " + $f) -ForegroundColor Red }

if (-not $KeepWorkRoot) {
    try {
        Invoke-Process -FilePath $gitExe -Arguments @('worktree', 'prune') -WorkingDirectory $repo | Out-Null
        Remove-Item -LiteralPath $WorkRoot -Recurse -Force
    } catch { }
} else {
    Write-Host ("work root kept at " + $WorkRoot)
}

if ($script:Failed -gt 0) { exit 1 }
exit 0
