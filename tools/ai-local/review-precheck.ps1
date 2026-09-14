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

# Where a reviewer command can plausibly live on Windows. Everything here is
# looked at, and what was found is written down, so that "no reviewer" is a
# reported fact with evidence rather than a guess.
function Get-ReviewerSearchRoots {
    $roots = @()
    if ($env:APPDATA)      { $roots += (Join-Path $env:APPDATA 'npm') }
    if ($env:LOCALAPPDATA) {
        $roots += (Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links')
        $roots += (Join-Path $env:LOCALAPPDATA 'pnpm')
        $roots += (Join-Path $env:LOCALAPPDATA 'Yarn\bin')
        $roots += (Join-Path $env:LOCALAPPDATA 'Programs')
        $roots += (Join-Path $env:LOCALAPPDATA 'Programs\codex')
        $roots += (Join-Path $env:LOCALAPPDATA 'npm')
    }
    if ($env:USERPROFILE) {
        $roots += (Join-Path $env:USERPROFILE '.codex\bin')
        $roots += (Join-Path $env:USERPROFILE '.codex\.sandbox-bin')
        $roots += (Join-Path $env:USERPROFILE '.codex')
        $roots += (Join-Path $env:USERPROFILE '.local\bin')
        $roots += (Join-Path $env:USERPROFILE '.cargo\bin')
        $roots += (Join-Path $env:USERPROFILE '.bun\bin')
        $roots += (Join-Path $env:USERPROFILE 'bin')
        $roots += (Join-Path $env:USERPROFILE 'AppData\Local\Programs')
    }
    if ($env:ProgramFiles)       { $roots += (Join-Path $env:ProgramFiles 'nodejs') }
    if (${env:ProgramFiles(x86)}) { $roots += (Join-Path ${env:ProgramFiles(x86)} 'nodejs') }
    $unique = @()
    $seen = @{}
    foreach ($r in $roots) {
        $key = $r.ToLowerInvariant()
        if (-not $seen.ContainsKey($key)) { $seen[$key] = $true; $unique += $r }
    }
    return $unique
}

function Find-ReviewerExecutables {
    param([Parameter(Mandatory=$true)][string]$Name)
    $found = @()

    # An explicit override is the answer, not a first guess. If someone names the
    # reviewer command, a different one must never be substituted quietly; if the
    # named file is not there, the honest result is "no reviewer".
    $override = ''
    if ($Name -eq 'codex')  { $override = $env:KACHA_CODEX_EXE }
    if ($Name -eq 'claude') { $override = $env:KACHA_CLAUDE_EXE }
    if ($override) {
        if (Test-Path -LiteralPath $override -PathType Leaf) { return @($override) }
        return @()
    }

    foreach ($suffix in @('', '.cmd', '.exe', '.bat', '.ps1')) {
        $cmd = Get-Command ($Name + $suffix) -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source) { $found += $cmd.Source }
    }
    # where.exe knows about PATHEXT entries Get-Command can miss.
    try {
        $where = Invoke-Process -FilePath 'where.exe' -Arguments @($Name) -WorkingDirectory $RepoRoot -TimeoutSeconds 30
        if ($where.ExitCode -eq 0) {
            foreach ($line in ($where.StdOut -split "`r?`n")) {
                if ($line.Trim().Length -gt 0) { $found += $line.Trim() }
            }
        }
    } catch { }

    foreach ($root in (Get-ReviewerSearchRoots)) {
        if (-not (Test-Path -LiteralPath $root)) { continue }
        foreach ($candidate in (Find-NamedExecutable -Root $root -Name $Name -Depth 3)) {
            $found += $candidate
        }
    }

    $unique = @()
    $seen = @{}
    foreach ($item in $found) {
        if (-not $item) { continue }
        $key = $item.ToLowerInvariant()
        if (-not $seen.ContainsKey($key)) { $seen[$key] = $true; $unique += $item }
    }
    # A command inside a .sandbox-bin directory is a shim that needs a host process
    # beside it. It answers --version and --help even when it cannot do any work,
    # so it is tried last, after every ordinary installation.
    $ordinary = @()
    $shims = @()
    foreach ($item in $unique) {
        if ($item -like '*\.sandbox-bin\*') { $shims += $item } else { $ordinary += $item }
    }
    return @($ordinary + $shims)
}

# A bounded walk. Depth is small on purpose: an unbounded search of a whole disk
# at every probe would cost more than the review it is trying to start.
function Find-NamedExecutable {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Name,
        [int]$Depth = 2
    )
    $hits = @()
    foreach ($suffix in @('.cmd', '.exe', '.bat', '.ps1')) {
        $candidate = Join-Path $Root ($Name + $suffix)
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $hits += $candidate }
    }
    if ($Depth -le 0) { return $hits }
    try {
        foreach ($child in @(Get-ChildItem -LiteralPath $Root -Directory -Force -ErrorAction SilentlyContinue)) {
            # Package trees are large and never hold the launcher we want.
            if ($child.Name -eq 'node_modules' -or $child.Name -eq '.git') { continue }
            $hits += (Find-NamedExecutable -Root $child.FullName -Name $Name -Depth ($Depth - 1))
        }
    } catch { }
    return $hits
}

# A written record of where we looked. Without it, "no reviewer found" is not
# actionable from the other side of the machine.
function Write-ReviewerSearchReport {
    $roots = @()
    foreach ($root in (Get-ReviewerSearchRoots)) {
        $exists = Test-Path -LiteralPath $root
        $entries = @()
        if ($exists) {
            try {
                foreach ($file in @(Get-ChildItem -LiteralPath $root -File -ErrorAction SilentlyContinue)) {
                    if ($file.Name -like 'codex*' -or $file.Name -like 'claude*') { $entries += $file.Name }
                }
            } catch { }
        }
        $allFiles = @()
        if ($exists -and ($root -like '*.codex*')) {
            # The one place worth listing in full: a shim here explains a reviewer
            # that starts and then cannot read anything.
            try {
                foreach ($file in @(Get-ChildItem -LiteralPath $root -File -Force -ErrorAction SilentlyContinue)) {
                    $allFiles += $file.Name
                }
            } catch { }
        }
        $roots += [pscustomobject]@{
            path = $root; exists = $exists
            reviewer_like_files = $entries; all_files = $allFiles
        }
    }
    $report = [pscustomobject]@{
        schema_version = 1
        kind           = 'reviewer_search'
        probed_utc     = Get-UtcStamp
        path_variable  = $env:PATH
        codex_found    = @(Find-ReviewerExecutables -Name 'codex')
        claude_found   = @(Find-ReviewerExecutables -Name 'claude')
        roots          = $roots
    }
    Write-JsonAtomic -Path (Join-Path $paths.Logs 'reviewer-search.json') -Value $report | Out-Null
    return $report
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
    # A sandbox shim answers --version and --help perfectly and then cannot read a
    # single file, because its host process is not installed. That is not a usable
    # reviewer, and it must not be allowed to look like one.
    if ($probe.probe_ok -and ($Exe -like '*\.sandbox-bin\*')) {
        $hostExe = Join-Path (Split-Path -Parent $Exe) 'codex-code-mode-host.exe'
        if (-not (Test-Path -LiteralPath $hostExe)) {
            $probe.probe_ok = $false
            $probe.probe_note = 'a sandbox shim with no codex-code-mode-host.exe beside it: it cannot read files'
        }
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
    Write-ReviewerSearchReport | Out-Null
    $best = $null
    foreach ($candidate in (Find-ReviewerExecutables -Name 'codex')) {
        $probe = Get-CodexInterface -Exe $candidate
        if ($null -eq $best) { $best = $probe }
        if ($probe.probe_ok) { $best = $probe; break }
    }
    if ($null -eq $best) {
        $best = [pscustomobject]@{
            schema_version = 1; kind = 'codex_interface'; probed_utc = Get-UtcStamp
            executable = ''; version = ''; has_exec = $false; supported_flags = @()
            help_excerpt = ''; probe_ok = $false
            probe_note = 'no codex executable found; see .ai-runtime/logs/reviewer-search.json for every place that was looked at'
        }
    }
    Write-JsonAtomic -Path $paths.Interface -Value $best | Out-Null
    return $best
}

# The sanctioned stand-in. .ai/ORCHESTRATOR_CONFIG.json already names a read-only
# fallback reviewer, so when Codex cannot run on this machine the queue does not
# simply stop. A fallback review is always recorded as a fallback review; it is
# never presented as Codex.
function Get-FallbackReviewerName {
    if ($env:KACHA_REVIEW_FALLBACK) { return $env:KACHA_REVIEW_FALLBACK }
    $configPath = Join-Path $RepoRoot '.ai\ORCHESTRATOR_CONFIG.json'
    $config = Read-JsonFile -Path $configPath
    if ($null -eq $config) { return 'none' }
    foreach ($p in $config.PSObject.Properties) {
        if ($p.Name -eq 'reviewer_fallback' -and $p.Value) { return [string]$p.Value }
    }
    return 'none'
}

function Get-ClaudeInterface {
    param([string]$Exe)
    $probe = [ordered]@{
        schema_version  = 1
        kind            = 'claude_interface'
        probed_utc      = Get-UtcStamp
        executable      = $Exe
        version         = ''
        supported_flags = @()
        help_excerpt    = ''
        probe_ok        = $false
        probe_note      = ''
    }
    try {
        $v = Invoke-Process -FilePath $Exe -Arguments @('--version') -WorkingDirectory $RepoRoot -TimeoutSeconds 60
        if ($v.ExitCode -eq 0) { $probe.version = $v.StdOut.Trim() }
    } catch {
        $probe.probe_note = 'version probe failed: ' + $_.Exception.Message
        return [pscustomobject]$probe
    }
    $help = $null
    try {
        $help = Invoke-Process -FilePath $Exe -Arguments @('--help') -WorkingDirectory $RepoRoot -TimeoutSeconds 60
    } catch {
        $probe.probe_note = 'help probe failed: ' + $_.Exception.Message
        return [pscustomobject]$probe
    }
    $text = ($help.StdOut + "`n" + $help.StdErr)
    $found = @()
    foreach ($flag in @('--print', '--permission-mode', '--permission-prompts', '--allowed-tools', '--model')) {
        if ($text -like ('*' + $flag + '*')) { $found += $flag }
    }
    if ($text -match '(?m)(^|\s)-p(\s|,|$)') { $found += '-p' }
    $probe.supported_flags = $found
    $excerptLength = [Math]::Min(4000, $text.Length)
    $probe.help_excerpt = $text.Substring(0, $excerptLength)
    $probe.probe_ok = (($found -contains '-p') -or ($found -contains '--print'))
    if (-not $probe.probe_ok -and -not $probe.probe_note) {
        $probe.probe_note = 'this installation does not advertise a one-shot print mode'
    }
    return [pscustomobject]$probe
}

function Resolve-ClaudeInterface {
    param([switch]$Force)
    $cachePath = Join-Path $paths.Logs 'claude-interface.json'
    if (-not $Force -and (Test-Path -LiteralPath $cachePath)) {
        $cached = Read-JsonFile -Path $cachePath
        if ($cached -and $cached.executable -and (Test-Path -LiteralPath $cached.executable)) {
            return $cached
        }
    }
    $best = $null
    foreach ($candidate in (Find-ReviewerExecutables -Name 'claude')) {
        $probe = Get-ClaudeInterface -Exe $candidate
        if ($null -eq $best) { $best = $probe }
        if ($probe.probe_ok) { $best = $probe; break }
    }
    if ($null -eq $best) {
        $best = [pscustomobject]@{
            schema_version = 1; kind = 'claude_interface'; probed_utc = Get-UtcStamp
            executable = ''; version = ''; supported_flags = @(); help_excerpt = ''
            probe_ok = $false; probe_note = 'no claude executable found'
        }
    }
    Write-JsonAtomic -Path $cachePath -Value $best | Out-Null
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
    $fallbackName = Get-FallbackReviewerName
    $fallback = $null
    if ($fallbackName -eq 'claude') { $fallback = Resolve-ClaudeInterface -Force:$Refresh }
    if (-not $codex.probe_ok) {
        if ($fallback -and $fallback.probe_ok) {
            # Not a problem that stops the queue, but it is written down every time.
            Write-AiLog -Message ('precheck: codex is not usable (' + $codex.probe_note +
                '); the sanctioned fallback reviewer will be used') -Level 'WARN' `
                -LogPath $paths.Dispatcher -Quiet:$Quiet
        } else {
            $problems += ('AIR-E003 no usable reviewer command: ' + $codex.probe_note)
        }
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
        fallback_name  = $fallbackName
        fallback       = $fallback
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
