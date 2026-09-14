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

# Bump this whenever what the probe CHECKS changes. A cached answer written by an
# older set of checks is not an answer to the current question, and quietly
# trusting it is how a shim stayed "usable" for two whole review attempts.
$script:ProbeRevision = 2

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
    # Some copies of the command are internal plumbing of the application rather
    # than the command a person would run: the sandbox shim needs a host process
    # beside it, and the plugin app-server is not a CLI at all. Both answer
    # --version and --help perfectly, so they are tried last, after every ordinary
    # installation such as ...\Programs\OpenAI\Codex\bin\codex.exe.
    $ordinary = @()
    $internal = @()
    foreach ($item in $unique) {
        $isInternal = ($item -like '*\.sandbox-bin\*') -or ($item -like '*\.plugin-appserver\*')
        if ($isInternal) { $internal += $item } else { $ordinary += $item }
    }
    return @($ordinary + $internal)
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
        probe_revision   = $script:ProbeRevision
        probed_utc       = Get-UtcStamp
        executable       = $Exe
        override         = [string]$env:KACHA_CODEX_EXE
        executable_stamp = ''
        version          = ''
        has_exec         = $false
        supported_flags  = @()
        help_excerpt     = ''
        probe_ok         = $false
        probe_note       = ''
    }
    try {
        $item = Get-Item -LiteralPath $Exe -ErrorAction Stop
        $probe.executable_stamp = ($item.Length.ToString() + '@' + $item.LastWriteTimeUtc.Ticks.ToString())
    } catch { }
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
    # A reviewer that cannot be held to read-only is not a reviewer we can use.
    # Checking the worktree afterwards only catches what it did inside the
    # worktree; nothing catches what it did outside.
    if ($probe.probe_ok -and ($found -notcontains '--sandbox')) {
        $probe.probe_ok = $false
        $probe.probe_note = 'this installation cannot be held to a read-only sandbox (no --sandbox), so it is not used'
    }
    if ($probe.probe_ok -and ($Exe -like '*\.sandbox-bin\*')) {
        $hostExe = Join-Path (Split-Path -Parent $Exe) 'codex-code-mode-host.exe'
        if (-not (Test-Path -LiteralPath $hostExe)) {
            $probe.probe_ok = $false
            $probe.probe_note = 'a sandbox shim with no codex-code-mode-host.exe beside it: it cannot read files'
        }
    }
    return [pscustomobject]$probe
}

# An upgrade replaces the file in place. A remembered answer about the old file
# is not an answer about the new one.
# A remembered answer belongs to the question that produced it. Naming a
# different reviewer, or clearing the name, is a different question: the old
# answer must not be reused, or an explicit override would be quietly ignored.
function Test-CachedOverrideMatches {
    param($Cached, [string]$Override)
    $cachedOverride = ''
    foreach ($p in $Cached.PSObject.Properties) {
        if ($p.Name -eq 'override') { $cachedOverride = [string]$p.Value }
    }
    return ($cachedOverride -eq $Override)
}

function Test-CachedExecutableUnchanged {
    param($Cached)
    $stamp = ''
    foreach ($p in $Cached.PSObject.Properties) {
        if ($p.Name -eq 'executable_stamp') { $stamp = [string]$p.Value }
    }
    if (-not $stamp) { return $false }
    try {
        $item = Get-Item -LiteralPath $Cached.executable -ErrorAction Stop
        return ($stamp -eq ($item.Length.ToString() + '@' + $item.LastWriteTimeUtc.Ticks.ToString()))
    } catch {
        return $false
    }
}

function Resolve-CodexInterface {
    param([switch]$Force)
    if (-not $Force -and (Test-Path -LiteralPath $paths.Interface)) {
        $cached = Read-JsonFile -Path $paths.Interface
        $cachedRevision = 0
        if ($cached) {
            foreach ($p in $cached.PSObject.Properties) {
                if ($p.Name -eq 'probe_revision') { $cachedRevision = [int]$p.Value }
            }
        }
        # A "yes" may be remembered; a "no" may not. Someone can install the
        # reviewer at any moment, and a remembered "no reviewer here" would keep
        # the queue stopped long after that stopped being true.
        if ($cached -and $cachedRevision -eq $script:ProbeRevision -and $cached.probe_ok -and
            $cached.executable -and (Test-Path -LiteralPath $cached.executable) -and
            (Test-CachedOverrideMatches -Cached $cached -Override ([string]$env:KACHA_CODEX_EXE)) -and
            (Test-CachedExecutableUnchanged -Cached $cached)) {
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
            schema_version = 1; kind = 'codex_interface'
            probe_revision = $script:ProbeRevision; probed_utc = Get-UtcStamp
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
        probe_revision  = $script:ProbeRevision
        probed_utc      = Get-UtcStamp
        executable      = $Exe
        override        = [string]$env:KACHA_CLAUDE_EXE
        executable_stamp = ''
        version         = ''
        supported_flags = @()
        help_excerpt    = ''
        probe_ok        = $false
        probe_note      = ''
    }
    try {
        $item = Get-Item -LiteralPath $Exe -ErrorAction Stop
        $probe.executable_stamp = ($item.Length.ToString() + '@' + $item.LastWriteTimeUtc.Ticks.ToString())
    } catch { }
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
    if ($probe.probe_ok -and ($found -notcontains '--permission-mode')) {
        $probe.probe_ok = $false
        $probe.probe_note = 'this installation cannot be held to a read-only plan mode (no --permission-mode), so it is not used'
    }
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
        $cachedRevision = 0
        if ($cached) {
            foreach ($p in $cached.PSObject.Properties) {
                if ($p.Name -eq 'probe_revision') { $cachedRevision = [int]$p.Value }
            }
        }
        if ($cached -and $cachedRevision -eq $script:ProbeRevision -and $cached.probe_ok -and
            $cached.executable -and (Test-Path -LiteralPath $cached.executable) -and
            (Test-CachedOverrideMatches -Cached $cached -Override ([string]$env:KACHA_CLAUDE_EXE)) -and
            (Test-CachedExecutableUnchanged -Cached $cached)) {
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
            schema_version = 1; kind = 'claude_interface'
            probe_revision = $script:ProbeRevision; probed_utc = Get-UtcStamp
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
    if ($fallbackName -eq 'claude') {
        $fallback = Resolve-ClaudeInterface -Force:$Refresh
    } else {
        # Say so in the file the runner reads. Leaving an older "yes" there would
        # let a switched-off fallback keep running.
        $fallback = [pscustomobject]@{
            schema_version = 1; kind = 'claude_interface'
            probe_revision = $script:ProbeRevision; probed_utc = Get-UtcStamp
            executable = ''; override = ''; executable_stamp = ''; version = ''
            supported_flags = @(); help_excerpt = ''; probe_ok = $false
            probe_note = ('the fallback reviewer is turned off (reviewer_fallback = ' + $fallbackName + ')')
        }
        Write-JsonAtomic -Path (Join-Path $paths.Logs 'claude-interface.json') -Value $fallback | Out-Null
    }
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

    # The id becomes a file name and a directory name in two places. Anything but
    # a plain name can point outside the runtime area and the worktree root.
    if (-not (Test-SafeRequestId -RequestId ([string]$manifest.request_id))) {
        $problems += ("AIR-E014 '" + [string]$manifest.request_id +
                      "' is not a plain name; a request id is used as a file and folder name")
    }
    $expectedName = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    if ($expectedName -ne [string]$manifest.request_id) {
        $problems += ("AIR-E015 the file is called '" + $expectedName + "' but says it is '" +
                      [string]$manifest.request_id + "'")
    }
    # The request must be about THIS checkout.
    try {
        $declaredRoot = [System.IO.Path]::GetFullPath([string]$manifest.repo_path).TrimEnd('\', '/')
        $actualRoot = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
        if ($declaredRoot.ToLowerInvariant() -ne $actualRoot.ToLowerInvariant()) {
            $problems += ("AIR-E016 this request was made for " + $declaredRoot + ", not " + $actualRoot)
        }
    } catch {
        $problems += 'AIR-E016 repo_path is not a usable path'
    }
    # A moving name such as HEAD or main is not a fixed review target.
    foreach ($field in @('base_commit', 'review_commit', 'tested_commit')) {
        $value = [string]$manifest.$field
        if ($value -notmatch '^[0-9a-f]{40}$') {
            $problems += ("AIR-E017 " + $field + " must be a full commit id, not '" + $value + "'")
        }
    }

    $damage = Get-ReviewLedgerDamage -RepoRoot $RepoRoot
    if ($damage -gt 0) {
        $problems += ("AIR-E061 the ledger has " + $damage +
                      " line(s) that cannot be read; it cannot answer whether this was reviewed already")
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
    if (Test-CommitAlreadyReviewed -RepoRoot $RepoRoot -RootRequestId $root -ReviewCommit $manifest.review_commit) {
        $problems += ('AIR-E041 ' + $root + ' has already been reviewed at ' +
                      (Get-ShortSha $manifest.review_commit) + '; the same commit is not reviewed twice')
    }
    $streak = Get-ConsecutiveBlockingCount -RepoRoot $RepoRoot -RootRequestId $root
    $humanNeeded = ($streak -ge 3)
    # Three blocking results in a row means the loop is not converging. Handing it
    # to a person has to actually stop the queue, not merely be noted.
    if ($humanNeeded) {
        $problems += ("AIR-E060 " + $root + " has been blocked " + $streak +
                      " times in a row; a person has to decide before another review is started")
    }

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
