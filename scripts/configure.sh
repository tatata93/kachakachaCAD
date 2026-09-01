#!/usr/bin/env bash
# 例: ./scripts/configure.sh            (OSごとの既定プリセット)
#     ./scripts/configure.sh linux-core (依存無しでコアだけ)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"
PRESET="$(resolve_preset "${1:-}")"
require_command cmake
echo "configure preset: $PRESET"
cmake --preset "$PRESET"
