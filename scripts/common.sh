#!/usr/bin/env bash
# Linux / macOS 用の共通処理。Windows の scripts/common.ps1 と対になる。
set -euo pipefail

repo_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

# 使うプリセットを決める。引数 > 環境変数 KACHACAD_PRESET > OSごとの既定。
resolve_preset() {
    local requested="${1:-}"
    if [ -n "$requested" ]; then
        echo "$requested"
        return
    fi
    if [ -n "${KACHACAD_PRESET:-}" ]; then
        echo "$KACHACAD_PRESET"
        return
    fi
    case "$(uname -s)" in
        Darwin) echo "macos-core" ;;
        *)      echo "linux" ;;
    esac
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "コマンドが見つかりません: $1" >&2
        return 1
    fi
}
