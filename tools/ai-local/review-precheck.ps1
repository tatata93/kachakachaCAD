<#
review-precheck.ps1 - decide whether this machine, and this one request, are in
a state where starting a reviewer is honest.

Two independent checks live here.

  -Machine       : can this machine review at all?  (git, codex, worktree room)
  -ManifestPath  : may THIS request be reviewed now? (schema, PASS evidence,
                   TESTED_HEAD == REVIEW_HEAD, commits present, not a duplicate)

The reviewer executable is discovered and PROBED, never assumed. Whatever
"codex exec --help" prints on this machine is what gets recorded in
.ai-runtime/logs/codex-interface.json, and review-runner.ps1 passes only the
options that were actually observed there. An option that this installation does
not advertise is not used.
#>

[CmdletBinding()]
param(
    [string]$RepoRoot,
    [switch]$Machine,
    [string]$ManifestPath,
    [switch]$Refresh,
    [switch]$Quiet
)

Set-StrictMode -Version 1.0
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'review-common.ps1')
. (Join-Path $PSScriptRoot 'review-ledger.ps1')

$RepoRoot = Get-RepoRoot -Hint $RepoRoot
$paths = Initialize-AiRuntime -RepoRoot $RepoRoot

function New-CheckResult {
    param([bool]$Ok, [string]$Code, [string]$Message, $Data)
    return [pscustomobject]@{
        ok      = $Ok
        code    = $Code
        message = $Message
        data    = $Data
    }
}

function Get-CodexCandidates {
    $list = New-Object System.Collections.ArrayList
    if ($env:KACHA_CODEX_EXE) { [void]$list.Add($env:KACHA_CODEX_EXE) }
    foreach ($name in @('codex', 'codex.cmd', 'codex.exe')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) { [void]$list.Add($cmd.Source) }
    }
    $guesses = @()
    if ($env:APPDATA)      { $guesses += (Join-Path $env:APPDATA 'npm\codex.cmd') }
    if ($env:APPDATA)      { $guesses += (Join-Path $env:APPDATA 'npm\codex') }
    if ($env:LOCALAPPDATA) { $guesses += (Join-Path $env:LOCALAPPDATA 'Programs\codex\codex.exe') }
    if ($env:USERPROFILE)  { $guesses += (Join-Path $env:USERPROFILE '.codex\bin\codex.exe') }
    if ($env:USERPROFILE)  { $guesses += (Join-Path $env:USERPROFILE '.cargo\bin\codex.exe') }
    foreach ($g in $guesses) {
        if (Test-Path -LiteralPath $g) { [void]$list.Add($g) }
    }
    $seen = @{}
    $unique = @()
    foreach ($item in $list) {
        $key = $item.ToLowerInvariant()
        if (-not $seen.ContainsKey($key)) { $seen[$key] = $true; $unique += $item }
    }
    return $unique
}

# Read the real help text and keep only the flags it mentions. This is the whole
# point of the probe: never spell an option that this build does not have.
function Get-CodexInterface {
    param([string]$Exe)
    $probe = [ordered]@{
        schema_version   = 1
        kind             = 'codex_interface'
        probed_utc       = Get-UtcStamp
        executable       = $Exe
        version          = ''
        has_exec         = $false
        supported_flags  = @()
        help_excerpt     = ''
        probe_ok         = $false
        probe_note       = ''
    }
    $dir = $RepoRoot
    try {
        $v = Invoke-Process -FilePath $Exe -Arguments @('--version') -WorkingDirectory $dir -TimeoutSeconds 60
        if ($v.ExitCode -eq 0) { $probe.version = $v.StdOut.Trim() }
        else { $probe.probe_note = 'version probe exit ' + $v.ExitCode }
    } catch {
        $probe.probe_note = 'version probe failed: ' + $_.Exception.Message
        return [pscustomobject]$probe
    }
    $help = $null
    try {
        $help = Invoke-Process -FilePath $Exe -Arguments @('exec', '--help') -WorkingDirectory $dir -TimeoutSeconds 60
    } catch {
        $probe.probe_note = 'help probe failed: ' + $_.Exception.Message
        return [pscustomobject]$probe
    }
    $text = ($help.StdOut + "`n" + $help.StdErr)
    if ($help.ExitCode -eq 0 -or $text -match '(?m)^\s*Usage') { $probe.has_exec = $true }
    $interesting = @('--output-last-message', '--ephemeral', '--sandbox',
                     '--skip-git-repo-check', '--cd', '--full-auto',
                     '--json', '--color', '--model', '--config')
    $found = @()
    foreach ($flag in $interesting) {
        if ($text -like ('*' + $flag + '*')) { $found += $flag }
    }
    # Short flags need a word boundary; "-C" appears inside ordinary words.
    foreach ($pair in @(@('-C', '(?m)(^|\s)-C(\s|,|$)'), @('-c', '(?m)(^|\s)-c(\s|,|$)'))) {
        if ($text -match $pair[1]) { $found += $pair[0] }
    }
    $probe.supported_flags = $found
    $excerptLength = [Math]::Min(4000, $text.Length)
    $probe.help_excerpt = $text.Substring(0, $excerptLength)
    $probe.probe_ok = $probe.has_exec
    if (-not $probe.has_exec -and -not $probe.probe_note) {
        $probe.probe_note = 'this installation does not advertise "codex exec"'
    }
    return [pscustomobject]$probe
}

function Resolve-CodexInterface {
    param([switch]$Force)
    if (-not $Force -and (Test-Path -LiteralPath $paths.Interface)) {
        $cached = Read-JsonFile -Path $paths.Interface
        if ($cached -and $cached.executable -and (Test-Path -LiteralPath $cached.executable)) {
            return $cached
        }
    }
    $best = $null
    foreach ($candidate in (Get-CodexCandidates)) {
        $probe = Get-CodexInterface -Exe $candidate
        if ($null -eq $best) { $best = $probe }
        if ($probe.probe_ok) { $best = $probe; break }
    }
    if ($null -eq $best) {
        $best = [pscustomobject]@{
            schema_version = 1; kind = 'codex_interface'; probed_utc = Get-UtcStamp
            executable = ''; version = ''; has_exec = $false; supported_flags = @()
            help_excerpt = ''; probe_ok = $false
            probe_note = 'no codex executable found on PATH or in the usual install locations'
        }
    }
    Write-JsonAtomic -Path $paths.Interface -Value $best | Out-Null
    return $best
}

function Get-WorktreeRoot {
    if ($env:KACHA_AI_WORKTREE_ROOT) { return $env:KACHA_AI_WORKTREE_ROOT }
    $sibling = Join-Path (Split-Path -Parent $RepoRoot) 'kachakachaCAD-worktrees'
    if (Test-Path -LiteralPath $sibling) { return (Join-Path $sibling 'ai-review') }
    return $paths.Worktrees
}

function Invoke-MachinePrecheck {
    $problems = @()
    $gitExe = Resolve-GitExe
    $gitOk = $false
    try {
        $r = Invoke-Process -FilePath $gitExe -Arguments @('--version') -WorkingDirectory $RepoRoot -TimeoutSeconds 60
        $gitOk = ($r.ExitCode -eq 0)
    } catch { $gitOk = $false }
    if (-not $gitOk) { $problems += 'AIR-E001 git is not usable from this machine' }

    $insideRepo = $false
    if ($gitOk) {
        $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $gitExe -Arguments @('rev-parse', '--is-inside-work-tree')
        $insideRepo = ($r.ExitCode -eq 0 -and $r.StdOut.Trim() -eq 'true')
    }
    if (-not $insideRepo) { $problems += "AIR-E002 $RepoRoot is not a git working tree" }

    $codex = Resolve-CodexInterface -Force:$Refresh
    if (-not $codex.probe_ok) {
        $problems += ('AIR-E003 no usable reviewer command: ' + $codex.probe_note)
    }

    $worktreeRoot = Get-WorktreeRoot
    $worktreeOk = $true
    try {
        if (-not (Test-Path -LiteralPath $worktreeRoot)) {
            New-Item -ItemType Directory -Path $worktreeRoot -Force | Out-Null
        }
        $probeFile = Join-Path $worktreeRoot ('.write-probe-' + $PID)
        Set-Content -LiteralPath $probeFile -Value 'probe' -Encoding ASCII
        Remove-Item -LiteralPath $probeFile -Force
    } catch {
        $worktreeOk = $false
        $problems += ('AIR-E004 review worktree root is not writable: ' + $worktreeRoot)
    }

    $result = [pscustomobject]@{
        ok             = ($problems.Count -eq 0)
        checked_utc    = Get-UtcStamp
        repo_root      = $RepoRoot
        git_exe        = $gitExe
        codex          = $codex
        worktree_root  = $worktreeRoot
        worktree_ok    = $worktreeOk
        problems       = $problems
    }
    return $result
}

$RequiredManifestFields = @(
    'schema_version', 'kind', 'request_id', 'created_utc', 'repo_path',
    'base_commit', 'review_commit', 'tested_commit',
    'build_result', 'test_result'
)

function Invoke-ManifestPrecheck {
    param([Parameter(Mandatory=$true)][string]$Path)
    $problems = @()
    $manifest = Read-JsonFile -Path $Path
    if ($null -eq $manifest) {
        return New-CheckResult -Ok $false -Code 'AIR-E010' `
            -Message "review request is not readable JSON: $Path" -Data $null
    }
    foreach ($field in $RequiredManifestFields) {
        $has = $false
        foreach ($p in $manifest.PSObject.Properties) { if ($p.Name -eq $field) { $has = $true; break } }
        if (-not $has) { $problems += "AIR-E011 review request is missing '$field'" }
    }
    if ($problems.Count -gt 0) {
        return New-CheckResult -Ok $false -Code 'AIR-E011' -Message ($problems -join '; ') -Data $manifest
    }
    if ([int]$manifest.schema_version -ne 1) {
        $problems += ('AIR-E012 unsupported schema_version ' + $manifest.schema_version)
    }
    if ($manifest.kind -ne 'review_request') {
        $problems += ("AIR-E013 kind is '" + $manifest.kind + "', expected 'review_request'")
    }

    # The rule the owner set: a reviewer only ever looks at the exact commit the
    # machine really built and really tested.
    if ($manifest.tested_commit -ne $manifest.review_commit) {
        $problems += ('AIR-E020 TESTED_HEAD ' + (Get-ShortSha $manifest.tested_commit) +
                      ' is not REVIEW_HEAD ' + (Get-ShortSha $manifest.review_commit) +
                      '; the reviewer is not started')
    }
    if ($manifest.build_result -ne 'PASS') {
        $problems += ('AIR-E021 build_result is ' + $manifest.build_result + '; this goes back to the implementer')
    }
    if ($manifest.test_result -ne 'PASS') {
        $problems += ('AIR-E022 test_result is ' + $manifest.test_result + '; this goes back to the implementer')
    }
    $selftest = ''
    foreach ($p in $manifest.PSObject.Properties) { if ($p.Name -eq 'selftest_result') { $selftest = [string]$manifest.selftest_result } }
    if ($selftest -and $selftest -ne 'PASS' -and $selftest -ne 'SKIPPED') {
        $problems += ('AIR-E023 selftest_result is ' + $selftest + '; this goes back to the implementer')
    }

    $gitExe = Resolve-GitExe
    if (-not (Test-CommitExists -RepoRoot $RepoRoot -Commit $manifest.review_commit -GitExe $gitExe)) {
        $problems += ('AIR-E030 REVIEW_HEAD ' + (Get-ShortSha $manifest.review_commit) + ' is not in this repository')
    }
    if (-not (Test-CommitExists -RepoRoot $RepoRoot -Commit $manifest.base_commit -GitExe $gitExe)) {
        $problems += ('AIR-E031 BASE ' + (Get-ShortSha $manifest.base_commit) + ' is not in this repository')
    }

    if (Test-AlreadyReviewed -RepoRoot $RepoRoot -RequestId $manifest.request_id) {
        $problems += ('AIR-E040 ' + $manifest.request_id + ' already has a recorded review; not reviewing it twice')
    }

    $root = Get-RootRequestId -RequestId $manifest.request_id
    $streak = Get-ConsecutiveBlockingCount -RepoRoot $RepoRoot -RootRequestId $root
    $humanNeeded = ($streak -ge 3)

    $ok = ($problems.Count -eq 0)
    $code = 'BLOCKED'
    $message = ($problems -join '; ')
    if ($ok) { $code = 'OK'; $message = 'ready to review' }
    $result = New-CheckResult -Ok $ok -Code $code -Message $message -Data $manifest
    Add-Member -InputObject $result -NotePropertyName 'problems' -NotePropertyValue $problems
    Add-Member -InputObject $result -NotePropertyName 'root_request_id' -NotePropertyValue $root
    Add-Member -InputObject $result -NotePropertyName 'consecutive_blocking' -NotePropertyValue $streak
    Add-Member -InputObject $result -NotePropertyName 'human_decision_required' -NotePropertyValue $humanNeeded
    return $result
}

if ($ManifestPath) {
    $r = Invoke-ManifestPrecheck -Path $ManifestPath
    if (-not $Quiet) { $r | ConvertTo-Json -Depth 8 }
    if ($r.ok) { exit 0 } else { exit 1 }
}

$machineResult = Invoke-MachinePrecheck
if (-not $Quiet) { $machineResult | ConvertTo-Json -Depth 8 }
if ($machineResult.ok) { exit 0 } else { exit 1 }
