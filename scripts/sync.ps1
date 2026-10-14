# 別PCで作業を始める前に実行する。ローカルの作業を退避したままGitHubに追従する。
param()

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "common.ps1")

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Push-Location $RepoRoot
try {
    $Branch = (git rev-parse --abbrev-ref HEAD).Trim()
    if (git status --porcelain --untracked-files=no) { throw "Tracked changes exist. Preserve their owner and use a separate worktree; sync will not stash or rebase them." }
    Invoke-Checked "git" @("fetch", "origin")
    Invoke-Checked "git" @("pull", "--ff-only", "origin", $Branch)
    git status --short
    git log --oneline -5
}
finally {
    Pop-Location
}
