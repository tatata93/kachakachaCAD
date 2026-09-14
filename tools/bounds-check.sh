#!/bin/sh
# 並びの外を読んでいないかを、雲側で捕まえる。
#
# Windows の Debug ビルドは `vector subscript out of range` で **止まる**。
# 雲側は Release + g++ なので、同じ間違いを黙って素通りしていた。
# 2026-09-14 に `TrimCurve` がそれで PC の CTest を止め、
# 往復を何度も無駄にした。libstdc++ の検査つきで同じことを雲で捕まえる。
#
# 使い方: sh tools/bounds-check.sh [ソース木のある場所]
set -e
root="${1:-.}"
build="${BOUNDS_BUILD_DIR:-/tmp/kacha-bounds}"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -D_GLIBCXX_ASSERTIONS -g" > /dev/null
cmake --build "$build" --parallel 8
( cd "$build" && ctest -E qt_stub -j 4 )
