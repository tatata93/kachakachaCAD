#!/bin/sh
# 画面のコードを、Qt の当て木だけで型検査する(-fsyntax-only)。
# 本物の組み立てではない。core の関数名の取り違えを、雲の側で捕まえるためのもの。
# 使い方: tools/qtstub/typecheck.sh <リポジトリの根> <コンパイラ>
#
# 検査するファイルは並べない。src/apps/cad_next/*.cpp を全部拾う。
# 並べていたころ、新しく足したファイルが漏れて、PC でしか落ちなかった。
# 漏れるくらいなら、余計に見るほうがよい。
set -e
ROOT="$1"
CXX="${2:-c++}"
STATUS=0
FOUND=0
for FILE in "$ROOT"/src/apps/cad_next/*.cpp
do
    [ -e "$FILE" ] || continue
    FOUND=$((FOUND + 1))
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
if [ "$FOUND" -eq 0 ]; then
    echo "typecheck FAILED: 画面のファイルが1つも見つかりません"
    exit 1
fi
if [ "$STATUS" -eq 0 ]; then
    echo "qt stub typecheck: OK ($FOUND files)"
fi
exit "$STATUS"
