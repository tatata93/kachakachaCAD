cmake_minimum_required(VERSION 3.20)

# --- 複数PC・複数OS対応 -------------------------------------------------
# このファイルに特定PCの絶対パスを書かないこと。
# Qt / vcpkg の場所は cmake/KachakachaEnvironment.cmake が
#   明示指定 > 環境変数 > local.cmake > OSごとの標準的な場所
# の順で解決する。詳細は docs/multi-machine-development.md。
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(KachakachaEnvironment)
kachakacha_include_local_config("${CMAKE_CURRENT_LIST_DIR}")
kachakacha_detect_vcpkg()   # project() より前に呼ぶ必要がある
# -----------------------------------------------------------------------

project(kachakachaCAD
    VERSION 0.1.0
    DESCRIPTION "Wireframe and sheet-material CAD for railway model making"
    LANGUAGES CXX
)

option(KACHACAD_BUILD_TESTS "Build kachakachaCAD tests" ON)
option(KACHACAD_BUILD_QT_APP "Build the Qt desktop application when Qt 6 is available" ON)
option(KACHACAD_ENABLE_OCCT "Enable Open CASCADE B-Rep and 3D export support" ON)

# テスト実行ファイル共通のコンパイル設定を1か所にまとめる。
# /utf-8 が無いと、日本語コメントを含むヘッダで MSVC が C4819 を大量に出す。
function(kachakacha_apply_test_options target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(KACHACAD_BUILD_TESTS)
    enable_testing()
    # 応答しなくなったテストでビルド全体が止まらないよう、既定の上限を設ける。
    # プリセット未使用の ctest 実行でも効くよう CMake 側にも入れておく。
    set(CTEST_TEST_TIMEOUT 600)
endif()

add_library(kachakacha_core STATIC
    src/core/kachakacha/geometry/Vector3.cpp
    src/core/kachakacha/io/BentSheetPapercraft.cpp
    src/core/kachakacha/io/FacetedPapercraft.cpp
    src/core/kachakacha/io/FabricationPanelPapercraft.cpp
    src/core/kachakacha/io/NumericExpression.cpp
    src/core/kachakacha/io/PartPatterns.cpp
    src/core/kachakacha/io/PlateFlatPattern.cpp
    src/core/kachakacha/io/PlanarExport.cpp
    src/core/kachakacha/io/ProjectScript.cpp
    src/core/kachakacha/model/Body.cpp
    src/core/kachakacha/model/Measurement.cpp
    src/core/kachakacha/model/PartModel.cpp
    src/core/kachakacha/model/Project.cpp
    src/core/kachakacha/model/Plate.cpp
    src/core/kachakacha/model/Sketch.cpp
    src/core/kachakacha/model/Surface.cpp
    src/core/kachakacha/model/Wire.cpp
    src/core/kachakacha/model/WireOperations.cpp
    src/core/kachakacha/model/WorkPlane.cpp
)

target_include_directories(kachakacha_core
    PUBLIC
        src/core
)

target_compile_features(kachakacha_core PUBLIC cxx_std_20)

if(MSVC)
    # /utf-8 が無いと、日本語コメントを含むヘッダで C4819 が大量に出る(既定コードページ932のため)。
    target_compile_options(kachakacha_core PRIVATE /W4 /permissive- /utf-8 /MP)
else()
    target_compile_options(kachakacha_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(KACHACAD_ENABLE_OCCT)
    # vcpkg のツールチェインを使う環境では、インストール済みのOCCTパッケージを最優先する。
    # 過去のキャッシュで OpenCASCADE_DIR が vcpkg の buildtrees(デバッグビルド木)を指したままだと、
    # デバッグ用 freetyped.dll に依存する DLL へリンクされ、exe の直接起動が
    # 「freetyped.dll が見つからない」で失敗する(2026-09-02 に実際に発生)。
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED OpenCASCADE_DIR
        AND OpenCASCADE_DIR MATCHES "buildtrees")
        message(STATUS "古い OpenCASCADE_DIR (${OpenCASCADE_DIR}) を無視し、vcpkg のOCCTを使います")
        unset(OpenCASCADE_DIR)
        unset(OpenCASCADE_DIR CACHE)
    endif()
    # 過去の失敗が NOTFOUND としてキャッシュされていると、OCCT設定ファイル内の
    # freetype 解決まで巻き添えで失敗し続ける。掃除してから探し直す。
    foreach(_kachacad_stale IN ITEMS
        FREETYPE_INCLUDE_DIR_freetype2
        FREETYPE_INCLUDE_DIR_ft2build
        FREETYPE_LIBRARY_DEBUG
        FREETYPE_LIBRARY_RELEASE)
        if(DEFINED ${_kachacad_stale} AND "${${_kachacad_stale}}" MATCHES "NOTFOUND")
            unset(${_kachacad_stale} CACHE)
        endif()
    endforeach()
    unset(_kachacad_stale)
    set(KACHACAD_OCCT_BUILDTREE_TARGETS "${OpenCASCADE_DIR}/OpenCASCADETargets.cmake")
    if(EXISTS "${KACHACAD_OCCT_BUILDTREE_TARGETS}"
        AND EXISTS "${OpenCASCADE_DIR}/include/opencascade"
        AND NOT EXISTS "${OpenCASCADE_DIR}/OpenCASCADEFoundationClassesTargets.cmake")
        include("${KACHACAD_OCCT_BUILDTREE_TARGETS}")
        set(OpenCASCADE_FOUND TRUE)
        set(OpenCASCADE_INCLUDE_DIR "${OpenCASCADE_DIR}/include/opencascade")
        set(KACHACAD_OCCT_VERSION "build tree")
    else()
        find_package(OpenCASCADE CONFIG QUIET COMPONENTS
            FoundationClasses
            ModelingData
            ModelingAlgorithms
            Visualization
            ApplicationFramework
            DataExchange
        )
        # 見つからない場合、vcpkg のインストール先を明示して探し直す。
        if(NOT (TARGET TKernel AND TARGET TKDESTEP)
            AND KACHACAD_VCPKG_ROOT AND DEFINED VCPKG_TARGET_TRIPLET
            AND EXISTS "${KACHACAD_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/share/opencascade/OpenCASCADEConfig.cmake")
            set(OpenCASCADE_DIR
                "${KACHACAD_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/share/opencascade"
                CACHE PATH "vcpkg の OpenCASCADE (自動検出)" FORCE)
            find_package(OpenCASCADE CONFIG COMPONENTS
                FoundationClasses
                ModelingData
                ModelingAlgorithms
                Visualization
                ApplicationFramework
                DataExchange
            )
        endif()
        if(TARGET TKernel AND TARGET TKOffset AND TARGET TKMesh
            AND TARGET TKDESTEP AND TARGET TKDESTL)
            set(OpenCASCADE_FOUND TRUE)
        else()
            message(STATUS
                "OpenCASCADE の探索に失敗: DIR=${OpenCASCADE_DIR} "
                "considered=${OpenCASCADE_CONSIDERED_CONFIGS}")
        endif()
        set(KACHACAD_OCCT_VERSION
            "${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}")
    endif()
    if(OpenCASCADE_FOUND)
        set(KACHACAD_OCCT_INCLUDE_DIR "${OpenCASCADE_INCLUDE_DIR}")
        if(NOT EXISTS "${KACHACAD_OCCT_INCLUDE_DIR}"
            AND EXISTS "${OpenCASCADE_DIR}/include/opencascade")
            set(KACHACAD_OCCT_INCLUDE_DIR "${OpenCASCADE_DIR}/include/opencascade")
        endif()
        add_library(kachakacha_occt STATIC
            src/occt/kachakacha/occt/BodyExport.cpp
            src/occt/kachakacha/occt/BodyExport.h
        )
        target_include_directories(kachakacha_occt
            PUBLIC
                src/occt
            PRIVATE
                ${KACHACAD_OCCT_INCLUDE_DIR}
        )
        target_link_libraries(kachakacha_occt
            PUBLIC
                kachakacha_core
            PRIVATE
                TKOffset
                TKBO
                TKMesh
                TKDESTEP
                TKDESTL
        )
        target_compile_features(kachakacha_occt PUBLIC cxx_std_20)
        if(MSVC)
            target_compile_options(kachakacha_occt PRIVATE /W4 /permissive- /utf-8 /MP)
        else()
            target_compile_options(kachakacha_occt PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        set(KACHACAD_OCCT_RUNTIME_PATH "$<TARGET_FILE_DIR:TKernel>")
        if(KACHACAD_VCPKG_ROOT AND VCPKG_TARGET_TRIPLET
            AND EXISTS "${KACHACAD_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
            string(APPEND KACHACAD_OCCT_RUNTIME_PATH
                "$<SEMICOLON>${KACHACAD_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/debug/bin"
                "$<SEMICOLON>${KACHACAD_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin")
        endif()
        message(STATUS "Open CASCADE shape/export layer enabled: ${KACHACAD_OCCT_VERSION}")
    else()
        message(WARNING "Open CASCADE was not found; the Qt CAD application requires it for verified STL/STEP export.")
    endif()
endif()

if(WIN32)
    add_executable(kachakacha_viewer
        src/apps/viewer/main.cpp
    )

    target_link_libraries(kachakacha_viewer
        PRIVATE
            kachakacha_core
            user32
            gdi32
    )

    target_compile_features(kachakacha_viewer PRIVATE cxx_std_20)
    if(MSVC)
        # 日本語コメントを含むヘッダを読むため /utf-8 が必須(既定コードページ932対策)。
        target_compile_options(kachakacha_viewer PRIVATE /utf-8 /MP)
    endif()
endif()

# The Qt desktop application is cross-platform: build it whenever Qt 6 and
# Open CASCADE are available, not only on Windows.
if(KACHACAD_BUILD_QT_APP)
        if(NOT DEFINED Qt6_DIR)
            kachakacha_detect_qt(KACHACAD_DETECTED_QT_ROOT)
            if(KACHACAD_DETECTED_QT_ROOT)
                list(PREPEND CMAKE_PREFIX_PATH "${KACHACAD_DETECTED_QT_ROOT}")
            endif()
        endif()

        find_package(Qt6 6.5 COMPONENTS Widgets QUIET)
        if(Qt6_FOUND AND TARGET kachakacha_occt)
            add_executable(kachakacha_cad WIN32
                src/apps/cad/CadViewport.cpp
                src/apps/cad/CadViewport.h
                src/apps/cad/MainWindow.cpp
                src/apps/cad/MainWindow.h
                src/apps/cad/MainWindowPartModel.cpp
                src/apps/cad/MainWindowSelfTest.cpp
                src/apps/cad/PartModelPanel.cpp
                src/apps/cad/PartModelPanel.h
                src/apps/cad/PartPatternViewDialog.cpp
                src/apps/cad/PartPatternViewDialog.h
                src/apps/cad/PlatePdfExport.cpp
                src/apps/cad/PlatePdfExport.h
                src/apps/cad/main.cpp
            )

            target_link_libraries(kachakacha_cad
                PRIVATE
                    kachakacha_core
                    kachakacha_occt
                    Qt6::Widgets
            )

            target_compile_features(kachakacha_cad PRIVATE cxx_std_20)
            if(MSVC)
                target_compile_options(kachakacha_cad PRIVATE /W4 /permissive- /utf-8 /MP)
            else()
                target_compile_options(kachakacha_cad PRIVATE -Wall -Wextra -Wpedantic)
            endif()

            qt_finalize_executable(kachakacha_cad)

            if(WIN32)
                add_custom_command(TARGET kachakacha_cad POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_RUNTIME_DLLS:kachakacha_cad>
                        $<TARGET_FILE_DIR:kachakacha_cad>
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                        $<TARGET_FILE_DIR:kachakacha_cad>/platforms
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>
                        $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
                        $<TARGET_FILE_DIR:kachakacha_cad>/platforms
                    COMMAND_EXPAND_LISTS
                    COMMENT "Copying Qt and Open CASCADE runtimes next to kachakacha_cad"
                )
            endif()
            message(STATUS "Qt desktop application enabled: ${Qt6_VERSION}")

            if(KACHACAD_BUILD_TESTS)
                get_filename_component(KACHACAD_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
                add_test(
                    NAME qt_cad_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        "QT_QPA_PLATFORM=offscreen"
                        "QT_PLUGIN_PATH=${KACHACAD_QT_PREFIX}/plugins"
                        "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>${KACHACAD_OCCT_RUNTIME_PATH}$<SEMICOLON>$ENV{PATH}"
                        $<TARGET_FILE:kachakacha_cad>
                        --self-test
                        --project "${CMAKE_SOURCE_DIR}/examples/first-check.kcd"
                )
                add_test(
                    NAME qt_surface_sample_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        "QT_QPA_PLATFORM=offscreen"
                        "QT_PLUGIN_PATH=${KACHACAD_QT_PREFIX}/plugins"
                        "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>${KACHACAD_OCCT_RUNTIME_PATH}$<SEMICOLON>$ENV{PATH}"
                        $<TARGET_FILE:kachakacha_cad>
                        --project "${CMAKE_SOURCE_DIR}/examples/curved-panel-light-holes.kcd"
                        --snapshot "${CMAKE_CURRENT_BINARY_DIR}/surface-sample-smoke.png"
                )
                add_test(
                    NAME qt_acceptance_model_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        "QT_QPA_PLATFORM=offscreen"
                        "QT_PLUGIN_PATH=${KACHACAD_QT_PREFIX}/plugins"
                        "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>${KACHACAD_OCCT_RUNTIME_PATH}$<SEMICOLON>$ENV{PATH}"
                        $<TARGET_FILE:kachakacha_cad>
                        --project "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
                        --snapshot "${CMAKE_CURRENT_BINARY_DIR}/railway-nose-acceptance.png"
                )

                # Qtアプリを起動するテストは、ダイアログ等で無応答になると
                # ctest が永久に待ってしまう。必ず上限を設ける。
                set_tests_properties(
                    qt_cad_smoke
                    qt_surface_sample_smoke
                    qt_acceptance_model_smoke
                    PROPERTIES TIMEOUT 300)

                add_executable(kachakacha_plate_pdf_tests
                    src/apps/cad/PlatePdfExport.cpp
                    src/apps/cad/PlatePdfExport.h
                    tests/plate_pdf_tests.cpp
                )
                target_include_directories(kachakacha_plate_pdf_tests PRIVATE src/apps/cad)
                target_link_libraries(kachakacha_plate_pdf_tests PRIVATE kachakacha_core Qt6::Gui)
                add_test(
                    NAME plate_pdf_tests
                    COMMAND ${CMAKE_COMMAND} -E env
                        "QT_QPA_PLATFORM=offscreen"
                        "QT_PLUGIN_PATH=${KACHACAD_QT_PREFIX}/plugins"
                        "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>${KACHACAD_OCCT_RUNTIME_PATH}$<SEMICOLON>$ENV{PATH}"
                        $<TARGET_FILE:kachakacha_plate_pdf_tests>
                        "${CMAKE_CURRENT_BINARY_DIR}/plate-pdf-test.pdf"
                        "${CMAKE_SOURCE_DIR}/examples/curved-panel-light-holes.kcd"
                )
    kachakacha_apply_test_options(kachakacha_plate_pdf_tests)
            endif()
        elseif(NOT Qt6_FOUND)
            message(WARNING "Qt 6.5 or newer was not found; kachakacha_cad will not be built.")
        else()
            message(WARNING "Open CASCADE was not found; kachakacha_cad will not be built.")
        endif()
endif()

if(KACHACAD_BUILD_TESTS)
    add_executable(kachakacha_numeric_expression_tests
        tests/numeric_expression_tests.cpp
    )

    target_link_libraries(kachakacha_numeric_expression_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_numeric_expression_tests)

    add_test(NAME numeric_expression_tests COMMAND kachakacha_numeric_expression_tests)

    add_executable(kachakacha_workplane_tests
        tests/workplane_tests.cpp
    )

    target_link_libraries(kachakacha_workplane_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_workplane_tests)

    add_test(NAME workplane_tests COMMAND kachakacha_workplane_tests)

    add_executable(kachakacha_sketch_wire_tests
        tests/sketch_wire_tests.cpp
    )

    target_link_libraries(kachakacha_sketch_wire_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_sketch_wire_tests)

    add_test(NAME sketch_wire_tests COMMAND kachakacha_sketch_wire_tests)

    add_executable(kachakacha_project_script_tests
        tests/project_script_tests.cpp
    )

    target_link_libraries(kachakacha_project_script_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_project_script_tests)

    add_test(
        NAME project_script_tests
        COMMAND kachakacha_project_script_tests
            "${CMAKE_SOURCE_DIR}/examples/115-front-guided-loft.kcd"
            "${CMAKE_SOURCE_DIR}/examples/er1-er2-round-cab-1-87.kcd"
    )

    add_executable(kachakacha_wire_operations_tests
        tests/wire_operations_tests.cpp
    )

    target_link_libraries(kachakacha_wire_operations_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_wire_operations_tests)

    add_test(NAME wire_operations_tests COMMAND kachakacha_wire_operations_tests)

    add_executable(kachakacha_measurement_tests
        tests/measurement_tests.cpp
    )

    target_link_libraries(kachakacha_measurement_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_measurement_tests)

    add_test(NAME measurement_tests COMMAND kachakacha_measurement_tests)

    add_executable(kachakacha_planar_export_tests
        tests/planar_export_tests.cpp
    )

    target_link_libraries(kachakacha_planar_export_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_planar_export_tests)

    add_test(NAME planar_export_tests COMMAND kachakacha_planar_export_tests)

    add_executable(kachakacha_plate_flat_pattern_tests
        tests/plate_flat_pattern_tests.cpp
    )

    target_link_libraries(kachakacha_plate_flat_pattern_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_plate_flat_pattern_tests)

    add_test(
        NAME plate_flat_pattern_tests
        COMMAND kachakacha_plate_flat_pattern_tests
            "${CMAKE_SOURCE_DIR}/examples/curved-panel-light-holes.kcd"
            "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
            "${CMAKE_CURRENT_BINARY_DIR}/railway-nose-papercraft.svg"
    )

    add_executable(kachakacha_faceted_papercraft_tests
        tests/faceted_papercraft_tests.cpp
    )

    target_link_libraries(kachakacha_faceted_papercraft_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_faceted_papercraft_tests)

    add_test(
        NAME faceted_papercraft_tests
        COMMAND kachakacha_faceted_papercraft_tests
            "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
            "${CMAKE_CURRENT_BINARY_DIR}/railway-nose-faceted-papercraft.svg"
    )

    add_executable(kachakacha_bent_sheet_papercraft_tests
        tests/bent_sheet_papercraft_tests.cpp
    )

    target_link_libraries(kachakacha_bent_sheet_papercraft_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_bent_sheet_papercraft_tests)

    add_test(
        NAME bent_sheet_papercraft_tests
        COMMAND kachakacha_bent_sheet_papercraft_tests
            "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
            "${CMAKE_CURRENT_BINARY_DIR}/railway-nose-bent-sheet-papercraft.svg"
    )

    add_executable(kachakacha_fabrication_panel_papercraft_tests
        tests/fabrication_panel_papercraft_tests.cpp
    )

    target_link_libraries(kachakacha_fabrication_panel_papercraft_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_fabrication_panel_papercraft_tests)

    add_test(
        NAME fabrication_panel_papercraft_tests
        COMMAND kachakacha_fabrication_panel_papercraft_tests
            "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
            "${CMAKE_SOURCE_DIR}/examples/er1-er2-round-cab-1-87.kcd"
            "${CMAKE_CURRENT_BINARY_DIR}/er1-er2-round-cab-panel-first.svg"
    )

    add_executable(kachakacha_part_model_tests
        tests/part_model_tests.cpp
    )

    target_link_libraries(kachakacha_part_model_tests
        PRIVATE
            kachakacha_core
    )

    kachakacha_apply_test_options(kachakacha_part_model_tests)
    add_test(NAME part_model_tests COMMAND kachakacha_part_model_tests)

    add_executable(kachakacha_surface_tests
        tests/surface_tests.cpp
    )

    target_link_libraries(kachakacha_surface_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_surface_tests)

    add_test(NAME surface_tests COMMAND kachakacha_surface_tests)

    add_executable(kachakacha_plate_tests
        tests/plate_tests.cpp
    )

    target_link_libraries(kachakacha_plate_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_plate_tests)

    add_test(NAME plate_tests COMMAND kachakacha_plate_tests)

    add_executable(kachakacha_body_tests
        tests/body_tests.cpp
    )

    target_link_libraries(kachakacha_body_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_body_tests)

    add_test(
        NAME body_tests
        COMMAND kachakacha_body_tests
            "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
    )

    if(TARGET kachakacha_occt)
        add_executable(kachakacha_occt_body_export_tests
            tests/occt_body_export_tests.cpp
        )
        target_link_libraries(kachakacha_occt_body_export_tests
            PRIVATE
                kachakacha_occt
        )
    kachakacha_apply_test_options(kachakacha_occt_body_export_tests)
        target_include_directories(kachakacha_occt_body_export_tests
            PRIVATE
                ${KACHACAD_OCCT_INCLUDE_DIR}
        )
        add_test(
            NAME occt_body_export_tests
            COMMAND ${CMAKE_COMMAND} -E env
                "PATH=${KACHACAD_OCCT_RUNTIME_PATH}$<SEMICOLON>$ENV{PATH}"
                $<TARGET_FILE:kachakacha_occt_body_export_tests>
                "${CMAKE_CURRENT_BINARY_DIR}/occt-body-export"
                "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
        )
    endif()

    if(WIN32)
        add_test(
            NAME viewer_snapshot
            COMMAND $<TARGET_FILE:kachakacha_viewer> --snapshot "${CMAKE_BINARY_DIR}/viewer-smoke.bmp"
        )

        add_test(
            NAME viewer_edit_smoke
            COMMAND $<TARGET_FILE:kachakacha_viewer> --self-test-edits
        )

        add_test(
            NAME viewer_project_edit_smoke
            COMMAND $<TARGET_FILE:kachakacha_viewer> --self-test-edits --project "${CMAKE_SOURCE_DIR}/examples/first-check.kcd"
        )

        add_test(
            NAME viewer_export_project
            COMMAND $<TARGET_FILE:kachakacha_viewer> --project "${CMAKE_SOURCE_DIR}/examples/first-check.kcd" --export "${CMAKE_BINARY_DIR}/viewer-export.kcd"
        )
    endif()
endif()
