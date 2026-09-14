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
  15 codex cannot run     -> the sanctioned fallback reviews, and says so
  16 a stale probe answer -> thrown away, not trusted
  17 a path filter        -> narrows what is shown, not what was built
  18 how deep to look     -> decided by what changed, not by the label
  19 a change too wide    -> refused whole, accepted in declared segments
  20 the same commit      -> not reviewed twice, even under a new number
  21 a reviewer that hangs-> stopped for real, recorded as TIMEOUT not FAIL
  22 Japanese in a diff   -> reaches the reviewer unbroken
  23 an unsafe request id -> refused at the door
  24 a contradictory answer-> MALFORMED, and the number is not spent
  25 a packet that is empty-> INFRA_ERROR, no reviewer started
  26 three blocks in a row -> the fourth try is refused, not reviewed
  27 looking at the queue  -> does not disturb the dispatcher's lock
  28 a long wait then a claim -> busy, not stuck
  29 arguments to a .cmd shim -> arrive the way a batch file expects
  30 a person clears a hold -> work starts again, and it is written down
  31 a ledger line cut off  -> the next one does not get glued to it
  32 danger is an area      -> the older model code counts too
  33 a .ps1 launcher       -> can actually be run
  34 the three-strike count-> only a real blocking verdict adds to it
  35 an impossible count   -> a broken answer, not a crash

It creates its own git repository under the temp directory, uses a stub reviewer,
and touches nothing in the real checkout.
#>

[CmdletBinding()]
param(
    # The PARENT to work under, not the work root itself. A uniquely named child is
    # created inside it and only that child is ever removed.
    [string]$WorkRoot,
    [switch]$KeepWorkRoot
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')
. (Join-Path $PSScriptRoot 'review-profile.ps1')

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

# This script deletes its work root when it is done. It therefore only ever works
# in a directory it created itself, under a unique name. Handing it an existing
# directory used to wipe that directory; a caller who passed a repository path by
# mistake would have lost it.
$workRootParent = $WorkRoot
if (-not $workRootParent) { $workRootParent = [System.IO.Path]::GetTempPath() }
if (-not (Test-Path -LiteralPath $workRootParent)) {
    New-Item -ItemType Directory -Path $workRootParent -Force | Out-Null
}
$WorkRoot = Join-Path $workRootParent ('kacha-review-selftest-' + [Guid]::NewGuid().ToString('N').Substring(0, 12))
if (Test-Path -LiteralPath $WorkRoot) {
    Write-Host ("FAIL the work root " + $WorkRoot + " already exists; refusing to touch it") -ForegroundColor Red
    exit 1
}
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
if ($argsList -contains "--help" -and -not ($argsList[0] -eq "exec")) {
    Write-Output "Usage: claude [options] [prompt]"
    Write-Output "  -p, --print                    print the answer and exit"
    Write-Output "      --permission-mode <MODE>   plan | acceptEdits"
    Write-Output "      --permission-prompts <M>   none | ask"
    exit 0
}
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
if ($verdict -eq "CONTRADICT") { $verdict = "PASS"; $next = "FIX_AND_REVIEW"; $blocking = 3 }
if ($verdict -eq "NONSENSE") { $verdict = "MAYBE"; $next = "PROCEED"; $blocking = 0 }
if ($verdict -eq "HUGECOUNT") { $verdict = "BLOCKING"; $next = "FIX_AND_REVIEW"; $blocking = "99999999999999999999" }
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
# The self-test must never reach a real reviewer installed on this machine.
$env:KACHA_REVIEW_FALLBACK = 'none'
$env:KACHA_CLAUDE_EXE = (Join-Path $WorkRoot 'no-such-claude.cmd')
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
    (($null -ne $noResult) -and $noResult.outcome -eq 'INFRA_ERROR') 'no INFRA_ERROR result'
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

# 15 ------------------------------------------------------------------------
# When codex cannot run here, the sanctioned fallback reviewer takes the request,
# and the result says plainly that it was the fallback.
$env:KACHA_CODEX_EXE = (Join-Path $WorkRoot 'no-such-reviewer.cmd')
$env:KACHA_CLAUDE_EXE = $goodStub
$env:KACHA_REVIEW_FALLBACK = 'claude'
Remove-Item -LiteralPath $paths.Interface -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $paths.Logs 'claude-interface.json') -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'fallback case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'fallback case')
$fbHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-FALLBACK-R1' -Base $baseCommit -Review $fbHead -Tested $fbHead | Out-Null
Run-Dispatcher | Out-Null
$fbResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-FALLBACK-R1.json')
Check 'the fallback reviewer takes the request when codex cannot run' `
    (($null -ne $fbResult) -and $fbResult.verdict -eq 'PASS') 'no PASS from the fallback'
Check 'the result says it was the fallback, not codex' `
    (($null -ne $fbResult) -and $fbResult.reviewer -eq 'claude-fallback') `
    ("reviewer=" + $(if ($fbResult) { $fbResult.reviewer } else { 'none' }))
$env:KACHA_CODEX_EXE = $goodStub
$env:KACHA_CLAUDE_EXE = (Join-Path $WorkRoot 'no-such-claude.cmd')
$env:KACHA_REVIEW_FALLBACK = 'none'

# 16 ------------------------------------------------------------------------
# An answer cached by an older set of checks is not an answer to today's question.
Write-JsonAtomic -Path $paths.Interface -Value ([pscustomobject]@{
    schema_version = 1; kind = 'codex_interface'; probe_revision = 0
    probed_utc = (Get-UtcStamp); executable = $goodStub; version = 'stale 0.0.0'
    has_exec = $true; supported_flags = @('--nonsense-flag'); help_excerpt = ''
    probe_ok = $true; probe_note = 'written by an older probe'
}) | Out-Null
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'stale probe case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'stale probe case')
$staleHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-STALE-R1' -Base $baseCommit -Review $staleHead -Tested $staleHead | Out-Null
Run-Dispatcher | Out-Null
$freshInterface = Read-JsonFile -Path $paths.Interface
Check 'a probe answer from an older set of checks is thrown away' `
    (($null -ne $freshInterface) -and ([int]$freshInterface.probe_revision) -gt 0) `
    ("probe_revision=" + $(if ($freshInterface) { $freshInterface.probe_revision } else { 'none' }))
Check 'the stale flag list is not used' `
    ((@($freshInterface.supported_flags) -notcontains '--nonsense-flag')) 'the stale flags survived'
$staleResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-STALE-R1.json')
Check 'the request is still reviewed after the stale answer is dropped' `
    (($null -ne $staleResult) -and $staleResult.verdict -eq 'PASS') 'no PASS'

# 17 ------------------------------------------------------------------------
# A request may narrow what the reviewer is shown without changing what was built.
New-Item -ItemType Directory -Path (Join-Path $repo 'inside') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $repo 'outside') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $repo 'inside\wanted.txt') -Value 'in scope' -Encoding ASCII
Set-Content -LiteralPath (Join-Path $repo 'outside\ignored.txt') -Value 'out of scope' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'path filter case')
$filterHead = (Git @('rev-parse', 'HEAD')).Trim()
$filterDecl = Join-Path $repo 'next-review-filter.json'
Write-JsonAtomic -Path $filterDecl -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request_declaration'
    request_id = 'T-PATHS-R1'; base_commit = $baseCommit; review_effort = 'LOW'
    scope_ja = 'path filter'; paths = @('inside/')
}) | Out-Null
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $filterDecl `
    -ReviewCommit $filterHead -TestedCommit $filterHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
Run-Dispatcher | Out-Null
$filterPacket = ''
$filterDiff = ''
$packetCopy = Join-Path (Join-Path $paths.Processing 'T-PATHS-R1') 'packet.md'
$diffCopy = Join-Path (Join-Path $paths.Processing 'T-PATHS-R1') 'diff.patch'
if (Test-Path -LiteralPath $packetCopy) { $filterPacket = [System.IO.File]::ReadAllText($packetCopy) }
if (Test-Path -LiteralPath $diffCopy) { $filterDiff = [System.IO.File]::ReadAllText($diffCopy) }
Check 'the narrowed packet holds the paths that were asked for' `
    ($filterDiff -like '*inside/wanted.txt*') 'the wanted path is missing'
Check 'the narrowed packet leaves the other paths out' `
    (-not ($filterDiff -like '*outside/ignored.txt*')) 'an out of scope path was shown'
Check 'the packet says that it was narrowed' `
    ($filterPacket -like '*This packet covers only*') 'the packet does not say so'
$filterResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-PATHS-R1.json')
Check 'a narrowed request still reviews the commit that was built' `
    (($null -ne $filterResult) -and $filterResult.review_commit -eq $filterHead) 'wrong review commit'

# 18 ------------------------------------------------------------------------
# How hard to look is decided by what changed, not by what the request calls it.
$quietProfile = Resolve-ReviewProfile -Declared '' -ChangedFiles @('docs/v2/notes.md') `
    -DiffText "+++ b/docs/v2/notes.md`n+a new sentence"
Check 'documents alone are a QUICK review' ($quietProfile.profile -eq 'QUICK') ("profile=" + $quietProfile.profile)
Check 'a QUICK review asks for low effort' ($quietProfile.effort -eq 'low') ("effort=" + $quietProfile.effort)

$riskyProfile = Resolve-ReviewProfile -Declared 'QUICK' `
    -ChangedFiles @('src/next/kachakacha/document/Document.cpp') `
    -DiffText "+++ b/src/next/kachakacha/document/Document.cpp`n+    BeginCompound();"
Check 'a change inside the document model is HIGH_RISK whatever it was called' `
    ($riskyProfile.profile -eq 'HIGH_RISK') ("profile=" + $riskyProfile.profile)
Check 'raising the profile says why' ($riskyProfile.reason -like '*raised from QUICK*') ("reason=" + $riskyProfile.reason)

$askedHigher = Resolve-ReviewProfile -Declared 'HIGH_RISK' -ChangedFiles @('docs/v2/notes.md') `
    -DiffText "+++ b/docs/v2/notes.md`n+a new sentence"
Check 'a request may ask for a deeper look than the machine measured' `
    ($askedHigher.profile -eq 'HIGH_RISK') ("profile=" + $askedHigher.profile)

$contextOnly = Resolve-ReviewProfile -Declared '' -ChangedFiles @('tools/ai-local/x.ps1') `
    -DiffText " BeginCompound();`n+Write-Host 'hello'"
Check 'a risky name only in the surrounding context does not raise the profile' `
    ($contextOnly.profile -ne 'HIGH_RISK') ("profile=" + $contextOnly.profile)

# 19 ------------------------------------------------------------------------
# A change too wide to read in one sitting is not handed over whole.
$wideDecl = Join-Path $repo 'next-review-wide.json'
New-Item -ItemType Directory -Path (Join-Path $repo 'wide') -Force | Out-Null
for ($i = 1; $i -le 6; $i++) {
    $lines = @()
    for ($j = 1; $j -le 40; $j++) { $lines += ("line " + $j) }
    Set-Content -LiteralPath (Join-Path $repo ('wide\file' + $i + '.txt')) -Value $lines -Encoding ASCII
}
Git @('add', '-A'); Git @('commit', '-q', '-m', 'a wide change')
$wideHead = (Git @('rev-parse', 'HEAD')).Trim()
Write-JsonAtomic -Path $wideDecl -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request_declaration'
    request_id = 'T-WIDE-R1'; base_commit = $baseCommit; scope_ja = 'wide'
}) | Out-Null
$env:KACHA_MAX_REVIEW_LINES = '100'
$beforeWide = Stub-CallCount
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $wideDecl `
    -ReviewCommit $wideHead -TestedCommit $wideHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
Check 'a change too wide for one review is not queued' `
    (-not (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-WIDE-R1.json'))) 'it was queued anyway'
Check 'the reason for refusing a wide change is written down' `
    (Test-Path -LiteralPath (Join-Path $paths.Failed 'T-WIDE-R1.json')) 'no refusal record'
Run-Dispatcher | Out-Null
Check 'a refused wide change starts no reviewer' ((Stub-CallCount) -eq $beforeWide) ("calls=" + (Stub-CallCount))

# The same change, declared in segments, goes through as separate reviews.
Write-JsonAtomic -Path $wideDecl -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request_declaration'
    request_id = 'T-SEG-R1'; base_commit = $baseCommit; scope_ja = 'segmented'
    segments = @(
        [pscustomobject]@{ name = 'A'; paths = @('wide/file1.txt', 'wide/file2.txt') },
        [pscustomobject]@{ name = 'B'; paths = @('wide/file3.txt', 'wide/file4.txt') }
    )
}) | Out-Null
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $wideDecl `
    -ReviewCommit $wideHead -TestedCommit $wideHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
Check 'each declared segment becomes its own request' `
    ((Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-SEG-A-R1.json')) -and
     (Test-Path -LiteralPath (Join-Path $paths.Incoming 'T-SEG-B-R1.json'))) 'a segment is missing'
$beforeSeg = Stub-CallCount
Run-Dispatcher | Out-Null
Check 'the segments are reviewed once each' ((Stub-CallCount) -eq ($beforeSeg + 2)) ("calls=" + (Stub-CallCount))
$env:KACHA_MAX_REVIEW_LINES = ''

# 20 ------------------------------------------------------------------------
# The same commit is not reviewed twice, even under a new request id.
Write-JsonAtomic -Path $wideDecl -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request_declaration'
    request_id = 'T-SEG-A-R2'; base_commit = $baseCommit; scope_ja = 'same commit again'
    paths = @('wide/file1.txt')
}) | Out-Null
$beforeSame = Stub-CallCount
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $wideDecl `
    -ReviewCommit $wideHead -TestedCommit $wideHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
Run-Dispatcher | Out-Null
Check 'the same commit is not reviewed again under a new number' `
    ((Stub-CallCount) -eq $beforeSame) ("calls=" + (Stub-CallCount))

# 21 ------------------------------------------------------------------------
# A reviewer that will not finish is stopped, and that is not a failing review.
$slowStub = Join-Path $WorkRoot 'slow-stub.cmd'
@"
@echo off
if "%1"=="--version" ( echo codex-stub 0.0.0 & exit /b 0 )
echo slow-stub called >> "$stubLog"
if "%1"=="exec" if "%2"=="--help" (
  echo Usage: codex exec [OPTIONS] [PROMPT]
  echo   -C, --cd ^<DIR^>
  echo       --sandbox ^<MODE^>
  echo       --output-last-message ^<F^>
  exit /b 0
)
powershell -NoProfile -Command "Start-Sleep -Seconds 120"
exit /b 0
"@ | Set-Content -LiteralPath $slowStub -Encoding ASCII
$env:KACHA_CODEX_EXE = $slowStub
Remove-Item -LiteralPath $paths.Interface -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'slow case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'slow case')
$slowHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-SLOW-R1' -Base $baseCommit -Review $slowHead -Tested $slowHead | Out-Null
$slowStart = Get-Date
& (Join-Path $Tools 'review-dispatcher.ps1') -RepoRoot $repo -Once -TimeoutSeconds 5 -Quiet | Out-Null
$slowSeconds = ((Get-Date) - $slowStart).TotalSeconds
$slowResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-SLOW-R1.json')
Check 'a reviewer that will not finish is actually stopped' ($slowSeconds -lt 90) `
    ("it took " + [int]$slowSeconds + "s")
Check 'being stopped is recorded as a timeout, not as a verdict' `
    (($null -ne $slowResult) -and $slowResult.outcome -eq 'TIMEOUT') `
    ("outcome=" + $(if ($slowResult) { $slowResult.outcome } else { 'none' }))
Check 'a timeout does not use up the REQUEST_ID' `
    (-not (Test-AlreadyReviewed -RepoRoot $repo -RequestId 'T-SLOW-R1')) 'it was counted as reviewed'
$env:KACHA_CODEX_EXE = $goodStub
Remove-Item -LiteralPath $paths.Interface -Force -ErrorAction SilentlyContinue

# 22 ------------------------------------------------------------------------
# The fixed diff handed to the reviewer must be the fixed diff, byte for byte.
# Codex found this one: the output was being decoded with the console code page,
# so every Japanese word came back as rubbish and the reviewer read rubbish.
# The text is built from code points on purpose. These scripts must stay pure
# ASCII: Windows PowerShell 5.1 reads a file with no byte order mark as CP932, so
# a Japanese character written straight into a .ps1 breaks the parser. It broke
# this very file once.
function Get-Text { param([int[]]$Codes) $t = ''; foreach ($c in $Codes) { $t += [char]$c }; return $t }
$wordReview   = Get-Text @(0x30EC, 0x30D3, 0x30E5, 0x30FC)          # review
$wordRefusal  = Get-Text @(0x65AD, 0x308A, 0x65B9)                  # the way of refusing
$wordRadius   = Get-Text @(0x66F2, 0x3052, 0x534A, 0x5F84)          # bend radius
$wordScope    = Get-Text @(0x65E5, 0x672C, 0x8A9E, 0x306E, 0x898B, 0x51FA, 0x3057)  # Japanese heading
$japanesePath = Join-Path $repo 'nihongo.txt'
$japaneseLines = @(('1 ' + $wordReview), ('2 ' + $wordRefusal), ('3 ' + $wordRadius))
[System.IO.File]::WriteAllText($japanesePath, ($japaneseLines -join "`r`n"),
    (New-Object System.Text.UTF8Encoding($false)))
Git @('add', '-A'); Git @('commit', '-q', '-m', 'a change with japanese in it')
$jpHead = (Git @('rev-parse', 'HEAD')).Trim()
$jpDecl = Join-Path $repo 'next-review-jp.json'
Write-JsonAtomic -Path $jpDecl -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request_declaration'
    request_id = 'T-UTF8-R1'; base_commit = $baseCommit
    scope_ja = $wordScope; paths = @('nihongo.txt')
}) | Out-Null
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $jpDecl `
    -ReviewCommit $jpHead -TestedCommit $jpHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
Run-Dispatcher | Out-Null
$jpDiffPath = Join-Path (Join-Path $paths.Processing 'T-UTF8-R1') 'diff.patch'
$jpDiff = ''
if (Test-Path -LiteralPath $jpDiffPath) { $jpDiff = [System.IO.File]::ReadAllText($jpDiffPath) }
Check 'the packet keeps Japanese as Japanese' ($jpDiff -like ('*1 ' + $wordReview + '*')) 'the text came back mangled'
Check 'the packet keeps every changed line separate' `
    (($jpDiff -like ('*2 ' + $wordRefusal + '*')) -and ($jpDiff -like ('*3 ' + $wordRadius + '*'))) 'lines were lost or joined'
$jpRequestPath = Join-Path (Join-Path $paths.Processing 'T-UTF8-R1') 'request.md'
$jpRequest = ''
if (Test-Path -LiteralPath $jpRequestPath) { $jpRequest = [System.IO.File]::ReadAllText($jpRequestPath) }
Check 'what to look at survives into the request the reviewer reads' `
    ($jpRequest -like ('*' + $wordScope + '*')) 'the scope came back mangled'

# 23 ------------------------------------------------------------------------
# A request id becomes a file name and a folder name, so nothing else is accepted.
foreach ($bad in @('..', '../escape', 'a\\b', 'with space')) {
    $badDecl = Join-Path $repo 'next-review-bad.json'
    Write-JsonAtomic -Path $badDecl -Value ([pscustomobject]@{
        schema_version = 1; kind = 'review_request_declaration'
        request_id = $bad; base_commit = $baseCommit; scope_ja = 'bad id'
    }) | Out-Null
    & (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $badDecl `
        -ReviewCommit $headCommit -TestedCommit $headCommit -BuildResult 'PASS' -TestResult 'PASS' `
        -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
}
$queuedNames = @(Get-QueueFiles -Directory $paths.Incoming | ForEach-Object { $_.Name })
Check 'a request id that is not a plain name never reaches the queue' `
    ($queuedNames.Count -eq 0) ("queued=" + ($queuedNames -join ','))

# 24 ------------------------------------------------------------------------
# An answer that contradicts itself is not a result anyone can act on.
$env:KACHA_STUB_VERDICT = 'CONTRADICT'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'contradiction case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'contradiction case')
$badHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-CONTRADICT-R1' -Base $baseCommit -Review $badHead -Tested $badHead | Out-Null
Run-Dispatcher | Out-Null
$badResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-CONTRADICT-R1.json')
Check 'PASS with blocking items is not accepted as a PASS' `
    (($null -ne $badResult) -and $badResult.verdict -eq 'MALFORMED') `
    ("verdict=" + $(if ($badResult) { $badResult.verdict } else { 'none' }))
Check 'an answer that breaks the contract does not use up the REQUEST_ID' `
    (-not (Test-AlreadyReviewed -RepoRoot $repo -RequestId 'T-CONTRADICT-R1')) 'it was counted as reviewed'

$env:KACHA_STUB_VERDICT = 'NONSENSE'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'nonsense case' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'nonsense case')
$nonsenseHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-NONSENSE-R1' -Base $baseCommit -Review $nonsenseHead -Tested $nonsenseHead | Out-Null
Run-Dispatcher | Out-Null
$nonsenseResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-NONSENSE-R1.json')
Check 'a verdict that is not one of the three is not accepted' `
    (($null -ne $nonsenseResult) -and $nonsenseResult.verdict -eq 'MALFORMED') `
    ("verdict=" + $(if ($nonsenseResult) { $nonsenseResult.verdict } else { 'none' }))
$env:KACHA_STUB_VERDICT = 'PASS'

# 25 ------------------------------------------------------------------------
# A packet that could not be built is not handed over as "nothing changed".
$emptyDecl = Join-Path $repo 'next-review-empty.json'
Write-JsonAtomic -Path $emptyDecl -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request_declaration'
    request_id = 'T-EMPTY-R1'; base_commit = $baseCommit; scope_ja = 'nothing here'
    paths = @('no/such/path/at/all')
}) | Out-Null
& (Join-Path $Tools 'review-enqueue.ps1') -RepoRoot $repo -DeclarationPath $emptyDecl `
    -ReviewCommit $nonsenseHead -TestedCommit $nonsenseHead -BuildResult 'PASS' -TestResult 'PASS' `
    -SelfTestResult 'PASS' -Branch 'work' -Quiet | Out-Null
$beforeEmpty = Stub-CallCount
Run-Dispatcher | Out-Null
$emptyResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-EMPTY-R1.json')
Check 'an empty packet is an infrastructure error, not a clean change' `
    (($null -ne $emptyResult) -and $emptyResult.outcome -eq 'INFRA_ERROR') `
    ("outcome=" + $(if ($emptyResult) { $emptyResult.outcome } else { 'none' }))
Check 'no reviewer is started for an empty packet' ((Stub-CallCount) -eq $beforeEmpty) ("calls=" + (Stub-CallCount))

# 26 ------------------------------------------------------------------------
# Three blocking results in a row must actually stop the queue, not just be noted.
$env:KACHA_STUB_VERDICT = 'BLOCKING'
for ($n = 1; $n -le 3; $n++) {
    Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value ("stop-loop-" + $n) -Encoding ASCII
    Git @('add', '-A'); Git @('commit', '-q', '-m', ("stop loop " + $n))
    $h = (Git @('rev-parse', 'HEAD')).Trim()
    Enqueue -RequestId ('T-STOPLOOP-R' + $n) -Base $baseCommit -Review $h -Tested $h | Out-Null
    Run-Dispatcher | Out-Null
}
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'stop-loop-4' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'stop loop 4')
$fourthHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-STOPLOOP-R4' -Base $baseCommit -Review $fourthHead -Tested $fourthHead | Out-Null
$beforeFourth = Stub-CallCount
Run-Dispatcher | Out-Null
Check 'a fourth try after three blocks is refused, not reviewed' `
    ((Stub-CallCount) -eq $beforeFourth) ("calls=" + (Stub-CallCount))
Check 'the refusal is written down where a person will see it' `
    (Test-Path -LiteralPath (Join-Path $paths.Failed 'T-STOPLOOP-R4.json')) 'no refusal record'
$env:KACHA_STUB_VERDICT = 'PASS'

# 27 ------------------------------------------------------------------------
# Looking at the queue must not disturb the lock. The test runs against an
# UNLOCKED file on purpose: with the lock held, a wrong implementation would fail
# to open it and the test would pass for the wrong reason. Content and time are
# both compared, because an overwrite of the same length changes neither size.
$lockPath = Join-Path $paths.Locks 'dispatcher.lock'
$ownerPath = $lockPath + '.owner.json'
Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
$marker = 'do-not-touch-' + [Guid]::NewGuid().ToString('N')
[System.IO.File]::WriteAllText($lockPath, $marker, (New-Object System.Text.UTF8Encoding($false)))
(Get-Item -LiteralPath $lockPath).LastWriteTime = (Get-Date).AddHours(-1)
$stampBefore = (Get-Item -LiteralPath $lockPath).LastWriteTimeUtc.Ticks
& (Join-Path $Tools 'queue-status.ps1') -RepoRoot $repo -Json | Out-Null
$contentAfter = [System.IO.File]::ReadAllText($lockPath)
$stampAfter = (Get-Item -LiteralPath $lockPath).LastWriteTimeUtc.Ticks
Check 'looking at the queue does not rewrite the lock' ($contentAfter -eq $marker) 'the lock content changed'
Check 'looking at the queue does not even touch the lock' ($stampBefore -eq $stampAfter) 'the lock time changed'

Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $ownerPath -Force -ErrorAction SilentlyContinue
$held = New-SingletonLock -Path $lockPath -RepoRootForOwner $repo
Check 'the lock can be taken' ($null -ne $held) 'the lock was not free'
if ($held) {
    # The holder keeps the lock exclusive, so who holds it has to be readable
    # from somewhere else. This is what stop-stale-dispatcher.ps1 relies on.
    $owner = Get-SingletonLockOwner -Path $lockPath
    Check 'who holds the lock can be read while it is held' `
        (($null -ne $owner) -and ([int]$owner.pid -eq $PID)) `
        ("owner=" + $(if ($owner) { $owner.pid } else { 'none' }))
    Check 'the owner says which checkout it belongs to' `
        (($null -ne $owner) -and $owner.repo_root -eq $repo) `
        ("repo_root=" + $(if ($owner) { $owner.repo_root } else { 'none' }))
    Check 'a held lock reads as held' (Test-SingletonLockHeld -Path $lockPath) 'it read as free'
    $held.Dispose()
}
Remove-Item -LiteralPath $ownerPath -Force -ErrorAction SilentlyContinue
Check 'a free lock reads as free' (-not (Test-SingletonLockHeld -Path $lockPath)) 'it read as held'

# 28 ------------------------------------------------------------------------
# A request that waited a long time and was claimed a moment ago is busy, not stuck.
$oldWait = Join-Path $paths.Ready 'T-WAITED-R1.json'
Write-JsonAtomic -Path $oldWait -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_request'; request_id = 'T-WAITED-R1'
    root_request_id = 'T-WAITED'; created_utc = (Get-UtcStamp); repo_path = $repo
    base_commit = $baseCommit; review_commit = $fourthHead; tested_commit = $fourthHead
    build_result = 'PASS'; test_result = 'PASS'; selftest_result = 'PASS'
    timeout_seconds = 600
}) | Out-Null
# Make it look like it has been sitting there for hours, the way a real backlog does.
(Get-Item -LiteralPath $oldWait).LastWriteTime = (Get-Date).AddHours(-5)
$claimed = Join-Path $paths.Processing 'T-WAITED-R1.json'
Move-QueueItemAtomic -Source $oldWait -Destination $claimed | Out-Null
(Get-Item -LiteralPath $claimed).LastWriteTime = (Get-Date)
Write-JsonAtomic -Path (Join-Path $paths.Processing 'T-WAITED-R1.json.owner') -Value ([pscustomobject]@{
    schema_version = 1; kind = 'review_claim'; request_id = 'T-WAITED-R1'
    pid = $PID; machine = $env:COMPUTERNAME; claimed_utc = (Get-UtcStamp)
}) | Out-Null
& (Join-Path $Tools 'review-recover.ps1') -RepoRoot $repo -Quiet | Out-Null
Check 'a claim made moments ago is left alone however long it waited before' `
    (Test-Path -LiteralPath $claimed) 'a healthy claim was taken away'
Remove-Item -LiteralPath $claimed -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $paths.Processing 'T-WAITED-R1.json.owner') -Force -ErrorAction SilentlyContinue

# 29 ------------------------------------------------------------------------
# Arguments must reach a .cmd shim the way a batch file expects them. Quoting all
# of them makes %1 arrive as \"exec\" instead of exec, and then the shim falls
# through to whatever comes next; that made the probe sleep for two minutes.
$echoStub = Join-Path $WorkRoot 'echo-stub.cmd'
$echoOut = Join-Path $WorkRoot 'echo-stub-out.txt'
@"
@echo off
if "%1"=="exec" ( echo FIRST-IS-EXEC >> "$echoOut" ) else ( echo FIRST-IS-%1 >> "$echoOut" )
echo SECOND-IS-%2 >> "$echoOut"
exit /b 0
"@ | Set-Content -LiteralPath $echoStub -Encoding ASCII
Invoke-Process -FilePath $echoStub -Arguments @('exec', 'a&b') -WorkingDirectory $WorkRoot -TimeoutSeconds 60 | Out-Null
$echoText = ''
if (Test-Path -LiteralPath $echoOut) { $echoText = [System.IO.File]::ReadAllText($echoOut) }
Check 'a plain argument reaches a .cmd shim unquoted' ($echoText -like '*FIRST-IS-EXEC*') `
    ("output=" + $echoText.Trim())
Check 'an argument with an ampersand survives instead of becoming a second command' `
    ($echoText -like '*SECOND-IS-*a&b*') ("output=" + $echoText.Trim())

# And the probe itself must have understood the shim, which is the thing that
# actually broke: if it had not, supported_flags would be empty.
$probedInterface = Read-JsonFile -Path $paths.Interface
Check 'the probe learned the options from the shim' `
    (($null -ne $probedInterface) -and (@($probedInterface.supported_flags).Count -gt 0)) `
    'the probe came back with no options'

# 30 ------------------------------------------------------------------------
# A rule that hands work to a person must let that person hand it back.
$heldRoot = 'T-STOPLOOP'
$streakBefore = Get-ConsecutiveBlockingCount -RepoRoot $repo -RootRequestId $heldRoot
Check 'three blocks in a row are counted' ($streakBefore -ge 3) ("streak=" + $streakBefore)
& (Join-Path $Tools 'clear-hold.ps1') -RepoRoot $repo -RootRequestId $heldRoot -Note 'self test' | Out-Null
$streakAfter = Get-ConsecutiveBlockingCount -RepoRoot $repo -RootRequestId $heldRoot
Check 'a person can clear the hold' ($streakAfter -eq 0) ("streak=" + $streakAfter)
$env:KACHA_STUB_VERDICT = 'PASS'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'after the hold' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'after the hold')
$afterHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-STOPLOOP-R5' -Base $baseCommit -Review $afterHead -Tested $afterHead | Out-Null
Run-Dispatcher | Out-Null
Check 'work starts again once a person has cleared it' `
    (Test-Path -LiteralPath (Join-Path $paths.Results 'T-STOPLOOP-R5.json')) 'it is still held'
Check 'clearing a hold is written down, not hidden' `
    (Test-Path -LiteralPath (Join-Path (Join-Path $paths.Root 'human-decisions') ($heldRoot + '.cleared.json'))) `
    'no record of the decision'

# 31 ------------------------------------------------------------------------
# A ledger line that was cut off must not swallow the next one.
$brokenLedger = (Get-AiRuntimePaths -RepoRoot $repo).Ledger
$before = [System.IO.File]::ReadAllText($brokenLedger)
[System.IO.File]::AppendAllText($brokenLedger, '{"event":"half_written_line",')
Add-ReviewLedgerEntry -RepoRoot $repo -Entry @{ event = 'review_started'; request_id = 'T-AFTER-BREAK' } | Out-Null
$damage = Get-ReviewLedgerDamage -RepoRoot $repo
Check 'a line that follows a broken one starts on its own line' ($damage -le 1) ("damaged=" + $damage)
$found = @(Get-ReviewLedgerEntries -RepoRoot $repo -RequestId 'T-AFTER-BREAK')
Check 'the line written after the break can still be read back' ($found.Count -eq 1) ("found=" + $found.Count)
# One unreadable line must not stop everything for ever. The queue keeps working.
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'after the damage' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'after the damage')
$damagedHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-AFTERDAMAGE-R1' -Base $baseCommit -Review $damagedHead -Tested $damagedHead | Out-Null
Run-Dispatcher | Out-Null
Check 'work carries on even though a ledger line is unreadable' `
    (Test-Path -LiteralPath (Join-Path $paths.Results 'T-AFTERDAMAGE-R1.json')) 'the queue stopped instead'
Check 'the unreadable line is kept where a person can see it' `
    (Test-Path -LiteralPath (Join-Path $paths.Logs 'review-ledger.damaged.jsonl')) 'it was not kept'

# 32 ------------------------------------------------------------------------
# Danger is an area, not one tree. A small careful change in the older model code
# is exactly as dangerous as one in the newer.
$v1Profile = Resolve-ReviewProfile -Declared '' -ChangedFiles @('src/core/kachakacha/model/Part.cpp') `
    -DiffText "diff --git a/src/core/kachakacha/model/Part.cpp b/src/core/kachakacha/model/Part.cpp`n@@ -1 +1 @@`n+    width = 2.0;"
Check 'the older model code is HIGH_RISK too' ($v1Profile.profile -eq 'HIGH_RISK') ("profile=" + $v1Profile.profile)
# No backslash escapes in a PowerShell double-quoted string: \" does not escape a
# quote, it ends the string early and the rest becomes further arguments. That is
# how a diff line here turned into a parameter and stopped the whole self-test.
$uiDiff = "diff --git a/src/apps/cad_next/V2Toolbar.cpp b/src/apps/cad_next/V2Toolbar.cpp`n@@ -1 +1 @@`n+    button->setText(tr('Draw'));"
$uiProfile = Resolve-ReviewProfile -Declared '' -ChangedFiles @('src/apps/cad_next/V2Toolbar.cpp') -DiffText $uiDiff
Check 'a small button change is not HIGH_RISK' ($uiProfile.profile -ne 'HIGH_RISK') ("profile=" + $uiProfile.profile)

# 33 ------------------------------------------------------------------------
# A reviewer written as a .ps1 must actually be runnable, since discovery accepts one.
$psStub = Join-Path $WorkRoot 'ps-stub.ps1'
$psOut = Join-Path $WorkRoot 'ps-stub-out.txt'
@"
param()
Set-Content -LiteralPath '$psOut' -Value ('args=' + (@(`$args) -join '|')) -Encoding ASCII
exit 0
"@ | Set-Content -LiteralPath $psStub -Encoding UTF8
Invoke-Process -FilePath $psStub -Arguments @('exec', 'two') -WorkingDirectory $WorkRoot -TimeoutSeconds 60 | Out-Null
$psText = ''
if (Test-Path -LiteralPath $psOut) { $psText = [System.IO.File]::ReadAllText($psOut) }
Check 'a .ps1 launcher can be run at all' ($psText -like '*args=exec|two*') ("output=" + $psText.Trim())

# 34 ------------------------------------------------------------------------
# Nothing but a real blocking verdict may add to the three-strike count.
$countRoot = 'T-STREAK'
$env:KACHA_STUB_VERDICT = 'BLOCKING'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'streak-1' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'streak 1')
$s1 = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-STREAK-R1' -Base $baseCommit -Review $s1 -Tested $s1 | Out-Null
Run-Dispatcher | Out-Null
$afterOne = Get-ConsecutiveBlockingCount -RepoRoot $repo -RootRequestId $countRoot
Check 'a blocking verdict adds one' ($afterOne -eq 1) ("streak=" + $afterOne)

$env:KACHA_STUB_VERDICT = 'NONSENSE'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'streak-2' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'streak 2')
$s2 = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-STREAK-R2' -Base $baseCommit -Review $s2 -Tested $s2 | Out-Null
Run-Dispatcher | Out-Null
$afterBad = Get-ConsecutiveBlockingCount -RepoRoot $repo -RootRequestId $countRoot
Check 'an answer in the wrong shape does not add to the count' ($afterBad -eq 1) ("streak=" + $afterBad)

$env:KACHA_STUB_VERDICT = 'PASS'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'streak-3' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'streak 3')
$s3 = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-STREAK-R3' -Base $baseCommit -Review $s3 -Tested $s3 | Out-Null
Run-Dispatcher | Out-Null
$afterPass = Get-ConsecutiveBlockingCount -RepoRoot $repo -RootRequestId $countRoot
Check 'a passing review clears the count' ($afterPass -eq 0) ("streak=" + $afterPass)

# 35 ------------------------------------------------------------------------
# A count that does not fit in a number is a broken answer, not a crash.
$env:KACHA_STUB_VERDICT = 'HUGECOUNT'
Set-Content -LiteralPath (Join-Path $repo 'a.txt') -Value 'huge count' -Encoding ASCII
Git @('add', '-A'); Git @('commit', '-q', '-m', 'huge count')
$hugeHead = (Git @('rev-parse', 'HEAD')).Trim()
Enqueue -RequestId 'T-HUGE-R1' -Base $baseCommit -Review $hugeHead -Tested $hugeHead | Out-Null
Run-Dispatcher | Out-Null
$hugeResult = Read-JsonFile -Path (Join-Path $paths.Results 'T-HUGE-R1.json')
Check 'a count too big for a number is a broken answer' `
    (($null -ne $hugeResult) -and $hugeResult.verdict -eq 'MALFORMED') `
    ("verdict=" + $(if ($hugeResult) { $hugeResult.verdict } else { 'none' }))
$env:KACHA_STUB_VERDICT = 'PASS'

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host ("review pipeline self-test: {0} passed, {1} failed, {2} total" -f $script:Passed, $script:Failed, ($script:Passed + $script:Failed))
foreach ($f in $script:Failures) { Write-Host ("  - " + $f) -ForegroundColor Red }

# When something failed, show what the pipeline itself said. A self-test that
# reports "it did not work" without the reason is the same fault it is meant to
# catch. The work root is kept too, so the evidence is still on disk.
if ($script:Failed -gt 0) {
    Write-Host ''
    Write-Host '--- what the pipeline said (dispatcher log, last 60 lines) ---'
    if (Test-Path -LiteralPath $paths.Dispatcher) {
        $logLines = @([System.IO.File]::ReadAllLines($paths.Dispatcher))
        $from = [Math]::Max(0, $logLines.Count - 60)
        for ($i = $from; $i -lt $logLines.Count; $i++) { Write-Host ('  ' + $logLines[$i]) }
    } else {
        Write-Host '  (no dispatcher log)'
    }
    foreach ($kind in @('precheck-raw-*.txt', 'precheck-*.json')) {
        foreach ($file in @(Get-ChildItem -LiteralPath $paths.Logs -File -ErrorAction SilentlyContinue |
                            Where-Object { $_.Name -like $kind } | Select-Object -First 3)) {
            Write-Host ''
            Write-Host ("--- " + $file.Name + " ---")
            $text = [System.IO.File]::ReadAllText($file.FullName)
            $cut = [Math]::Min(1500, $text.Length)
            Write-Host ('  ' + $text.Substring(0, $cut).Replace("`n", "`n  "))
        }
    }
    Write-Host ''
    Write-Host ("the work root is kept for inspection: " + $WorkRoot)
    exit 1
}

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
