# review-common.ps1 - shared helpers for the local event-driven review pipeline.
#
# Windows PowerShell 5.1 compatible on purpose: no null-coalescing, no ternary,
# no ForEach-Object -Parallel, no ConvertFrom-Json -AsHashtable.
#
# Nothing in this file starts a reviewer. It only knows about paths, files,
# locks and JSON. Keeping it side-effect free is what makes review-selftest.ps1
# able to drive the pipeline against a throwaway repository.

Set-StrictMode -Version 1.0

$script:ReviewSchemaVersion = 1

function Get-RepoRoot {
    param([string]$Hint)
    if ($Hint -and (Test-Path -LiteralPath $Hint)) {
        return (Resolve-Path -LiteralPath $Hint).Path
    }
    # tools/ai-local/<this file> -> repository root is two levels up.
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
}

function Get-AiRuntimeRoot {
    param([Parameter(Mandatory=$true)][string]$RepoRoot)
    if ($env:KACHA_AI_RUNTIME) { return $env:KACHA_AI_RUNTIME }
    return (Join-Path $RepoRoot '.ai-runtime')
}

function Get-AiRuntimePaths {
    param([Parameter(Mandatory=$true)][string]$RepoRoot)
    $root = Get-AiRuntimeRoot -RepoRoot $RepoRoot
    return [pscustomobject]@{
        Root       = $root
        Incoming   = Join-Path $root 'incoming'
        Ready      = Join-Path $root 'ready'
        Processing = Join-Path $root 'processing'
        Results    = Join-Path $root 'results'
        Failed     = Join-Path $root 'failed'
        Stale      = Join-Path $root 'stale'
        Locks      = Join-Path $root 'locks'
        Logs       = Join-Path $root 'logs'
        Worktrees  = Join-Path $root 'worktrees'
        Ledger     = Join-Path (Join-Path $root 'logs') 'review-ledger.jsonl'
        Dispatcher = Join-Path (Join-Path $root 'logs') 'dispatcher.log'
        Interface  = Join-Path (Join-Path $root 'logs') 'codex-interface.json'
    }
}

function Initialize-AiRuntime {
    param([Parameter(Mandatory=$true)][string]$RepoRoot)
    $paths = Get-AiRuntimePaths -RepoRoot $RepoRoot
    foreach ($dir in @($paths.Root, $paths.Incoming, $paths.Ready, $paths.Processing,
                       $paths.Results, $paths.Failed, $paths.Stale, $paths.Locks,
                       $paths.Logs, $paths.Worktrees)) {
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
    }
    return $paths
}

function Get-UtcStamp {
    return (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
}

function Write-AiLog {
    param(
        [Parameter(Mandatory=$true)][string]$Message,
        [string]$Level = 'INFO',
        [string]$LogPath,
        [switch]$Quiet
    )
    $line = '{0} [{1}] {2}' -f (Get-UtcStamp), $Level, $Message
    if (-not $Quiet) { Write-Host $line }
    if ($LogPath) {
        $dir = Split-Path -Parent $LogPath
        if ($dir -and -not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Path $dir -Force | Out-Null
        }
        # Best effort. A locked log must never take the pipeline down.
        try { Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 } catch { }
    }
}

# Partial JSON is the classic failure of a file-drop queue: a reader sees a file
# that a writer has not finished. Every producer writes <name>.tmp first and
# renames afterwards, and no consumer ever looks at *.tmp.
function Write-JsonAtomic {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)]$Value,
        [int]$Depth = 12
    )
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $tmp = "$Path.tmp"
    $json = $Value | ConvertTo-Json -Depth $Depth
    # No BOM: the cloud side reads these files as plain UTF-8.
    [System.IO.File]::WriteAllText($tmp, $json, (New-Object System.Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    [System.IO.File]::Move($tmp, $Path)
    return $Path
}

function Write-TextAtomic {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Text
    )
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $tmp = "$Path.tmp"
    [System.IO.File]::WriteAllText($tmp, $Text, (New-Object System.Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    [System.IO.File]::Move($tmp, $Path)
    return $Path
}

function Read-JsonFile {
    param([Parameter(Mandatory=$true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    $text = [System.IO.File]::ReadAllText($Path)
    if (-not $text -or $text.Trim().Length -eq 0) { return $null }
    try { return ($text | ConvertFrom-Json) } catch { return $null }
}

function Get-QueueFiles {
    param([Parameter(Mandatory=$true)][string]$Directory)
    if (-not (Test-Path -LiteralPath $Directory)) { return @() }
    # PowerShell's own -like is used instead of -Filter: the Windows filter
    # matches extensions by prefix, which would also hand back "x.json.owner".
    # *.tmp is excluded by definition: those are half-written.
    return @(Get-ChildItem -LiteralPath $Directory -File |
             Where-Object { $_.Name -like '*.json' -and $_.Name -notlike '*.tmp' } |
             Sort-Object Name)
}

# Atomic claim. File.Move throws when the destination exists, and only one
# process can move a given source file, so the winner is decided by the
# filesystem rather than by a check-then-act race.
function Move-QueueItemAtomic {
    param(
        [Parameter(Mandatory=$true)][string]$Source,
        [Parameter(Mandatory=$true)][string]$Destination
    )
    try {
        [System.IO.File]::Move($Source, $Destination)
        return $true
    } catch {
        return $false
    }
}

function New-SingletonLock {
    param([Parameter(Mandatory=$true)][string]$Path)
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    try {
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::OpenOrCreate,
                                         [System.IO.FileAccess]::ReadWrite,
                                         [System.IO.FileShare]::None)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes(('pid={0} started={1}' -f $PID, (Get-UtcStamp)))
        $stream.SetLength(0)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        return $stream
    } catch {
        return $null
    }
}

function Test-ProcessAlive {
    param([Parameter(Mandatory=$true)][int]$ProcessId)
    if ($ProcessId -le 0) { return $false }
    try {
        $p = Get-Process -Id $ProcessId -ErrorAction Stop
        return ($null -ne $p)
    } catch {
        return $false
    }
}

# Windows PowerShell 5.1 has no ProcessStartInfo.ArgumentList, so arguments are
# quoted by hand following the Windows command line rules.
function ConvertTo-CommandLineArgument {
    param([Parameter(Mandatory=$true)][AllowEmptyString()][string]$Value)
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s""]') { return $Value }
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($ch in $Value.ToCharArray()) {
        if ($ch -eq '\') {
            $backslashes++
            continue
        }
        if ($ch -eq '"') {
            [void]$builder.Append('\' * ($backslashes * 2 + 1))
            [void]$builder.Append('"')
        } else {
            [void]$builder.Append('\' * $backslashes)
            [void]$builder.Append($ch)
        }
        $backslashes = 0
    }
    [void]$builder.Append('\' * ($backslashes * 2))
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-CommandLine {
    param([Parameter(Mandatory=$true)][AllowEmptyCollection()][string[]]$Arguments)
    $parts = @()
    foreach ($a in $Arguments) { $parts += (ConvertTo-CommandLineArgument -Value $a) }
    return ($parts -join ' ')
}

function Invoke-Process {
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory=$true)][string]$WorkingDirectory,
        [int]$TimeoutSeconds = 0
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    # CreateProcess cannot start a .cmd or .bat directly, and an npm-installed
    # reviewer is usually a .cmd shim. Route those through the command processor.
    $extension = [System.IO.Path]::GetExtension($FilePath)
    if ($extension -and ($extension.ToLowerInvariant() -eq '.cmd' -or $extension.ToLowerInvariant() -eq '.bat')) {
        $comspec = $env:ComSpec
        if (-not $comspec) { $comspec = 'cmd.exe' }
        $psi.FileName = $comspec
        $psi.Arguments = '/c ' + (ConvertTo-CommandLine -Arguments (@($FilePath) + $Arguments))
    } else {
        $psi.FileName = $FilePath
        $psi.Arguments = ConvertTo-CommandLine -Arguments $Arguments
    }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $WorkingDirectory
    $proc = [System.Diagnostics.Process]::Start($psi)
    # Read both pipes before waiting, otherwise a full pipe buffer deadlocks.
    $outTask = $proc.StandardOutput.ReadToEndAsync()
    $errTask = $proc.StandardError.ReadToEndAsync()
    $timedOut = $false
    if ($TimeoutSeconds -gt 0) {
        if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            try { $proc.Kill() } catch { }
        }
    }
    $proc.WaitForExit()
    $out = $outTask.Result
    $err = $errTask.Result
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        StdOut   = $out
        StdErr   = $err
        TimedOut = $timedOut
        CommandLine = ('{0} {1}' -f $psi.FileName, $psi.Arguments)
    }
}

function Invoke-Git {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string[]]$Arguments,
        [string]$GitExe
    )
    if (-not $GitExe) { $GitExe = Resolve-GitExe }
    return (Invoke-Process -FilePath $GitExe -Arguments $Arguments -WorkingDirectory $RepoRoot)
}

function Resolve-GitExe {
    if ($env:KACHA_GIT_EXE -and (Test-Path -LiteralPath $env:KACHA_GIT_EXE)) {
        return $env:KACHA_GIT_EXE
    }
    $candidates = @(
        'C:\Program Files\Git\cmd\git.exe',
        'C:\Program Files (x86)\Git\cmd\git.exe'
    )
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { return $c }
    }
    $found = Get-Command git -ErrorAction SilentlyContinue
    if ($found) { return $found.Source }
    return 'git'
}

function Test-CommitExists {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [Parameter(Mandatory=$true)][string]$Commit,
        [string]$GitExe
    )
    if (-not $Commit) { return $false }
    $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $GitExe -Arguments @('cat-file', '-e', ($Commit + '^{commit}'))
    return ($r.ExitCode -eq 0)
}

function Get-HeadCommit {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$GitExe
    )
    $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $GitExe -Arguments @('rev-parse', 'HEAD')
    if ($r.ExitCode -ne 0) { return '' }
    return $r.StdOut.Trim()
}

function Get-CurrentBranch {
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$GitExe
    )
    $r = Invoke-Git -RepoRoot $RepoRoot -GitExe $GitExe -Arguments @('rev-parse', '--abbrev-ref', 'HEAD')
    if ($r.ExitCode -ne 0) { return '' }
    return $r.StdOut.Trim()
}

function Get-ShortSha {
    param([string]$Sha)
    if (-not $Sha) { return '' }
    if ($Sha.Length -le 12) { return $Sha }
    return $Sha.Substring(0, 12)
}

function Get-RootRequestId {
    param([Parameter(Mandatory=$true)][string]$RequestId)
    # P1-EXTRUDE-R5 -> P1-EXTRUDE. A revision suffix never starts a new history.
    if ($RequestId -match '^(?<root>.+)-R(?<n>\d+)$') { return $Matches['root'] }
    return $RequestId
}

function Get-RequestAttempt {
    param([Parameter(Mandatory=$true)][string]$RequestId)
    if ($RequestId -match '^(?<root>.+)-R(?<n>\d+)$') { return [int]$Matches['n'] }
    return 1
}
