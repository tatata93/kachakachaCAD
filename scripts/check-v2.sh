#!/usr/bin/env bash
# Wire-first V2 の構成・ビルド・試験をまとめて回す。
# V2の受入試験(名前が v2_ で始まるもの)が1件も登録されていなければ失敗にする。
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"

PRESET="$(resolve_preset "${1:-}")"
# ビルド先はpresetのbinaryDirから引く。表を手で持つと、presetを足したときに必ずずれる。
case "$PRESET" in
    linux-core|windows-core|macos-core) BUILD_DIR="build-core" ;;
    linux)                              BUILD_DIR="build-linux" ;;
    windows-msvc)                       BUILD_DIR="build-msvc2022-x64" ;;
    *)
        echo "知らないpresetです: $PRESET" >&2
        exit 2
        ;;
esac

echo "== configure ($PRESET) =="
cmake --preset "$PRESET"

echo "== build =="
cmake --build --preset "$PRESET" --parallel

echo "== V2 tests registered? =="
V2_COUNT="$(ctest --test-dir "$BUILD_DIR" -N -R '^v2_' 2>/dev/null | grep -c 'Test *#' || true)"
echo "v2 tests found: ${V2_COUNT}"
if [ "${V2_COUNT}" -eq 0 ]; then
    echo "ERROR: no V2 test is registered. check-v2 requires at least one test named v2_*." >&2
    exit 2
fi

echo "== V2 tests =="
ctest --test-dir "$BUILD_DIR" -R '^v2_' --output-on-failure

echo "== existing tests (must not regress) =="
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "check-v2: OK"
