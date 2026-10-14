#!/usr/bin/env bash
# 別PCで作業を始める前に実行する。ローカルの作業を退避したままGitHubに追従する。
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"
require_command git
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Tracked changes exist. Preserve their owner and use a separate worktree; sync will not stash or rebase them." >&2
  exit 1
fi
git fetch origin
git pull --ff-only origin "$(git rev-parse --abbrev-ref HEAD)"
git status --short
git log --oneline -5
