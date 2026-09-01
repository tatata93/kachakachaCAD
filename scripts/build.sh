#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"
PRESET="$(resolve_preset "${1:-}")"
require_command cmake
echo "build preset: $PRESET"
cmake --build --preset "$PRESET" --parallel
