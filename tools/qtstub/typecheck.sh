#!/bin/sh
# 画面のコードを、Qt の当て木だけで型検査する(-fsyntax-only)。
# 本物の組み立てではない。core の関数名の取り違えを、雲の側で捕まえるためのもの。
# 使い方: tools/qtstub/typecheck.sh <リポジトリの根> <コンパイラ>
set -e
ROOT="$1"
CXX="${2:-c++}"
STATUS=0
for FILE in \
    "$ROOT/src/apps/cad_next/V2ExportDock.cpp" \
    "$ROOT/src/apps/cad_next/V2MainWindow.cpp" \
    "$ROOT/src/apps/cad_next/V2WireCommands.cpp" \
    "$ROOT/src/apps/cad_next/V2Viewport.cpp" \
    "$ROOT/src/apps/cad_next/V2ViewportPanel.cpp" \
    "$ROOT/src/apps/cad_next/main.cpp"
do
    if ! "$CXX" -std=c++20 -fsyntax-only -fmax-errors=8 \
        -I "$ROOT/tools/qtstub/include" \
        -I "$ROOT/src/next" \
        -I "$ROOT/src/next_occt" \
        -I "$ROOT/src/apps/cad_next" \
        "$FILE"
    then
        echo "typecheck FAILED: $FILE"
        STATUS=1
    fi
done
if [ "$STATUS" -eq 0 ]; then
    echo "qt stub typecheck: OK"
fi
exit "$STATUS"
