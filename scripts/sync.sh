#!/usr/bin/env bash
# 別PCで作業を始める前に実行する。ローカルの作業を退避したままGitHubに追従する。
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"
require_command git
git fetch origin
git pull --rebase --autostash origin "$(git rev-parse --abbrev-ref HEAD)"
git status --short
git log --oneline -5
