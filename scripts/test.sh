#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"
PRESET="$(resolve_preset "${1:-}")"
require_command ctest
echo "test preset: $PRESET"
ctest --preset "$PRESET"
