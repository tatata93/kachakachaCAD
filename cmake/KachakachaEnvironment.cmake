# 複数PC・複数OSで同じ手順が通るようにするための環境検出。
#
# 方針:
#   - このファイルにもCMakeLists.txtにも、特定PCの絶対パスを直接書かない。
#   - 検出順は「明示指定 > 環境変数 > local.cmake > OSごとの標準的な場所」。
#   - 見つからなくても構成は失敗させない。該当機能を無効にして先へ進む。
#
# 使う環境変数:
#   VCPKG_ROOT        vcpkgの場所
#   KACHACAD_QT_ROOT  Qt6のプレフィックス (例: C:/Qt/6.9.2/msvc2022_64, /opt/qt6)
#   QT_ROOT_DIR       同上 (Qt公式CIが使う名前。互換のため見る)

include_guard(GLOBAL)

# リポジトリ直下の local.cmake があれば読み込む。
# PC固有の設定(Qtの場所など)はこのファイルに書く。gitには入れない。
function(kachakacha_include_local_config repo_root)
    if(EXISTS "${repo_root}/local.cmake")
        message(STATUS "PC固有設定を読み込み: ${repo_root}/local.cmake")
        include("${repo_root}/local.cmake")
    endif()
endfunction()

# 候補リストのうち最初に存在したものを返す。
function(_kachakacha_first_existing out_var)
    foreach(candidate IN LISTS ARGN)
        if(candidate AND EXISTS "${candidate}")
            set(${out_var} "${candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# glob候補をバージョン降順に並べて返す。
function(_kachakacha_sorted_glob out_var)
    set(matches "")
    foreach(pattern IN LISTS ARGN)
        file(GLOB found LIST_DIRECTORIES true "${pattern}")
        list(APPEND matches ${found})
    endforeach()
    if(matches)
        list(REMOVE_DUPLICATES matches)
        list(SORT matches COMPARE NATURAL ORDER DESCENDING)
    endif()
    set(${out_var} "${matches}" PARENT_SCOPE)
endfunction()

# vcpkgを探し、まだツールチェインが指定されていなければ設定する。
# project() より前に呼ぶこと。
macro(kachakacha_detect_vcpkg)
    if(NOT DEFINED KACHACAD_VCPKG_ROOT OR KACHACAD_VCPKG_ROOT STREQUAL "")
        set(_kachacad_vcpkg_candidates
            "$ENV{VCPKG_ROOT}"
            "$ENV{USERPROFILE}/vcpkg"
            "$ENV{HOME}/vcpkg"
            "C:/vcpkg"
            "C:/dev/vcpkg"
            "/opt/vcpkg"
            "/usr/local/vcpkg"
        )
        set(KACHACAD_VCPKG_ROOT "")
        foreach(_kachacad_candidate IN LISTS _kachacad_vcpkg_candidates)
            if(_kachacad_candidate AND EXISTS "${_kachacad_candidate}/scripts/buildsystems/vcpkg.cmake")
                set(KACHACAD_VCPKG_ROOT "${_kachacad_candidate}")
                break()
            endif()
        endforeach()
        unset(_kachacad_candidate)
        unset(_kachacad_vcpkg_candidates)
    endif()

    if(KACHACAD_VCPKG_ROOT AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "${KACHACAD_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain (自動検出)")
        if(NOT DEFINED VCPKG_TARGET_TRIPLET)
            if(WIN32 OR CMAKE_HOST_WIN32)
                set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "vcpkg triplet (自動検出)")
            elseif(APPLE)
                set(VCPKG_TARGET_TRIPLET "x64-osx" CACHE STRING "vcpkg triplet (自動検出)")
            else()
                set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "vcpkg triplet (自動検出)")
            endif()
        endif()
        message(STATUS "vcpkg を検出: ${KACHACAD_VCPKG_ROOT} (${VCPKG_TARGET_TRIPLET})")
    endif()
endmacro()

# Qt6のプレフィックスを探し、CMAKE_PREFIX_PATH の先頭へ追加する。
function(kachakacha_detect_qt out_var)
    set(qt_root "")

    if(DEFINED KACHACAD_QT_ROOT AND NOT KACHACAD_QT_ROOT STREQUAL "")
        set(qt_root "${KACHACAD_QT_ROOT}")
    endif()

    if(qt_root STREQUAL "")
        _kachakacha_first_existing(qt_root
            "$ENV{KACHACAD_QT_ROOT}"
            "$ENV{QT_ROOT_DIR}"
        )
    endif()

    if(qt_root STREQUAL "")
        # OSごとの標準的な導入先を新しいバージョンから探す。
        if(WIN32)
            _kachakacha_sorted_glob(qt_candidates
                "C:/Qt/6.*/msvc*_64"
                "D:/Qt/6.*/msvc*_64"
                "$ENV{USERPROFILE}/Qt/6.*/msvc*_64"
            )
        elseif(APPLE)
            _kachakacha_sorted_glob(qt_candidates
                "$ENV{HOME}/Qt/6.*/macos"
                "/opt/qt6"
                "/opt/homebrew/opt/qt6"
                "/usr/local/opt/qt6"
            )
        else()
            _kachakacha_sorted_glob(qt_candidates
                "/opt/qt6"
                "/opt/Qt/6.*/gcc_64"
                "$ENV{HOME}/Qt/6.*/gcc_64"
            )
        endif()
        foreach(candidate IN LISTS qt_candidates)
            if(EXISTS "${candidate}/lib/cmake/Qt6")
                set(qt_root "${candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(NOT qt_root STREQUAL "" AND EXISTS "${qt_root}")
        message(STATUS "Qt 6 を検出: ${qt_root}")
    else()
        # 見つからなくてもよい。システム導入のQt(Linuxのdistroパッケージ等)は
        # find_package が自分で見つける。
        set(qt_root "")
    endif()

    set(${out_var} "${qt_root}" PARENT_SCOPE)
endfunction()
