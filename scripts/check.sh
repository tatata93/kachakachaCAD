#!/usr/bin/env bash
# 構成・ビルド・テストをまとめて回す。Windows の scripts/check.ps1 と同じ役割。
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"
PRESET="$(resolve_preset "${1:-}")"
./scripts/configure.sh "$PRESET"
./scripts/build.sh "$PRESET"
./scripts/test.sh "$PRESET"
