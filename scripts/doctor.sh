#!/usr/bin/env bash
# この環境で何がビルドできるかを表示する。Windows の scripts/doctor.ps1 と対になる。
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
cd "$(repo_root)"

echo "kachakachaCAD 開発環境チェック ($(uname -s))"
echo

ok=0
for tool in git cmake ctest ninja c++; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '[ok]      %-8s -> %s\n' "$tool" "$(command -v "$tool")"
    else
        printf '[missing] %s\n' "$tool"
        case "$tool" in git|cmake|c++) ok=1 ;; esac
    fi
done

echo
echo "任意の依存:"
qt_root="${KACHACAD_QT_ROOT:-${QT_ROOT_DIR:-}}"
if [ -z "$qt_root" ]; then
    for candidate in /opt/qt6 /opt/Qt/6.*/gcc_64 "$HOME"/Qt/6.*/gcc_64; do
        [ -d "$candidate/lib/cmake/Qt6" ] && qt_root="$candidate" && break
    done
fi
if [ -n "$qt_root" ] && [ -d "$qt_root" ]; then
    echo "[ok]      Qt 6            -> $qt_root"
else
    echo "[missing] Qt 6            (KACHACAD_QT_ROOT を設定するか linux-core プリセットを使う)"
fi

occt_found=""
for candidate in "${VCPKG_ROOT:-}/installed" /opt/occt /usr/local/lib/cmake/opencascade /usr/lib/cmake/opencascade; do
    [ -n "$candidate" ] && [ -e "$candidate" ] && occt_found="$candidate" && break
done
if [ -n "$occt_found" ]; then
    echo "[ok]      Open CASCADE    -> $occt_found"
else
    echo "[missing] Open CASCADE    (docs/linux-build.md 参照。無い場合は linux-core プリセット)"
fi

echo
if [ "$ok" -ne 0 ]; then
    echo "必須ツールが足りない。導入してからビルドすること。"
    exit 1
fi
echo "必須ツールは揃っている。"
