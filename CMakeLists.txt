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
    src/core/kachakacha/io/NumericExpression.cpp
    src/core/kachakacha/io/PartFoldState.cpp
    src/core/kachakacha/io/PartPatterns.cpp
    src/core/kachakacha/io/OutputMesh.cpp
    src/core/kachakacha/io/PlateFlatPattern.cpp
    src/core/kachakacha/io/PlanarExport.cpp
    src/core/kachakacha/io/ProjectScript.cpp
    src/core/kachakacha/model/AutoSurface.cpp
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
                src/apps/cad/CollapsibleSection.cpp
                src/apps/cad/CollapsibleSection.h
                src/apps/cad/CoreMessageJa.cpp
                src/apps/cad/CoreMessageJa.h
                src/apps/cad/MainWindow.cpp
                src/apps/cad/MainWindow.h
                src/apps/cad/MainWindowMeasure.cpp
                src/apps/cad/MainWindowOutput.cpp
                src/apps/cad/MainWindowPanels.cpp
                src/apps/cad/MainWindowPartModel.cpp
                src/apps/cad/MainWindowSelfTest.cpp
                src/apps/cad/MainWindowSurface.cpp
                src/apps/cad/MainWindowUiHelpers.h
                src/apps/cad/ModelTreeWidget.h
                src/apps/cad/OutputPreviewDialog.cpp
                src/apps/cad/OutputPreviewDialog.h
                src/apps/cad/PartModelPanel.cpp
                src/apps/cad/PartModelPanel.h
                src/apps/cad/PartPatternViewDialog.cpp
                src/apps/cad/PartPatternViewDialog.h
                src/apps/cad/PlatePdfExport.cpp
                src/apps/cad/PlatePdfExport.h
                src/apps/cad/SheetPartPanel.cpp
                src/apps/cad/SheetPartPanel.h
                src/apps/cad/Win95Style.cpp
                src/apps/cad/Win95Style.h
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
                # $<TARGET_RUNTIME_DLLS> は Qt と OCCT 本体しか含まない。
                # OCCT が使うサードパーティDLL(freetype.dll など)が無いと、
                # exe の直接起動が「freetype.dll が見つからない」で失敗する。
                if(KACHACAD_VCPKG_ROOT AND DEFINED VCPKG_TARGET_TRIPLET)
                    file(GLOB KACHACAD_VCPKG_THIRDPARTY_DLLS
                        "${KACHACAD_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
                    list(FILTER KACHACAD_VCPKG_THIRDPARTY_DLLS
                        EXCLUDE REGEX "/TK[^/]*\\.dll$")
                    if(KACHACAD_VCPKG_THIRDPARTY_DLLS)
                        add_custom_command(TARGET kachakacha_cad POST_BUILD
                            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                ${KACHACAD_VCPKG_THIRDPARTY_DLLS}
                                $<TARGET_FILE_DIR:kachakacha_cad>
                            COMMENT "Copying vcpkg third-party runtimes (freetype etc.)"
                        )
                    endif()
                endif()
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

    add_executable(kachakacha_output_mesh_tests
        tests/output_mesh_tests.cpp
    )

    target_link_libraries(kachakacha_output_mesh_tests
        PRIVATE
            kachakacha_core
    )
    kachakacha_apply_test_options(kachakacha_output_mesh_tests)

    add_test(NAME output_mesh_tests COMMAND kachakacha_output_mesh_tests)

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

endif()

# =======================================================================
# Wire-first V2 (移行中)
#
# 現行の kachakacha_cad と並行してビルドする。受入が全部通るまで現行版は消さない。
# ここより上の現行ターゲットに影響を与えないこと(WP-01の条件)。
# 依存方向: kachakacha_v2_core は Qt にも OCCT にも依存しない(AT-ARC-001)。
# =======================================================================

add_library(kachakacha_v2_core STATIC
    src/next/kachakacha/app/CommandCatalog.cpp
    src/next/kachakacha/app/CursorInput.cpp
    src/next/kachakacha/app/EvaluationQueue.cpp
    src/next/kachakacha/app/ExportService.cpp
    src/next/kachakacha/app/PointSources.cpp
    src/next/kachakacha/app/ProcessSteps.cpp
    src/next/kachakacha/app/CursorInputSolve.cpp
    src/next/kachakacha/app/DrawingSession.cpp
    src/next/kachakacha/app/OperationGuide.cpp
    src/next/kachakacha/app/UiMode.cpp
    src/next/kachakacha/base/Diagnostic.cpp
    src/next/kachakacha/base/Ids.cpp
    src/next/kachakacha/base/Uuid.cpp
    src/next/kachakacha/base/Version.cpp
    src/next/kachakacha/document/BrokenReference.cpp
    src/next/kachakacha/document/Commands.cpp
    src/next/kachakacha/document/Document.cpp
    src/next/kachakacha/document/FeatureReevaluation.cpp
    src/next/kachakacha/modeling/GridModel.cpp
    src/next/kachakacha/modeling/GuideSurfaceTable.cpp
    src/next/kachakacha/view/ViewOrientation.cpp
    src/next/kachakacha/modeling/GuideSurfaceInput.cpp
    src/next/kachakacha/modeling/ExtrudeInput.cpp
    src/next/kachakacha/exporters/PatternExport.cpp
    src/next/kachakacha/exporters/PdfWriter.cpp
    src/next/kachakacha/fabrication/Assembly.cpp
    src/next/kachakacha/fabrication/ClosedLoop.cpp
    src/next/kachakacha/fabrication/CurvatureAnalysis.cpp
    src/next/kachakacha/fabrication/FabricationSettings.cpp
    src/next/kachakacha/fabrication/OpeningClip.cpp
    src/next/kachakacha/fabrication/FreezeState.cpp
    src/next/kachakacha/fabrication/FreezeMaterialize.cpp
    src/next/kachakacha/fabrication/ManualRole.cpp
    src/next/kachakacha/fabrication/PanelStrategy.cpp
    src/next/kachakacha/fabrication/PatternLayout.cpp
    src/next/kachakacha/fabrication/ReliefCut.cpp
    src/next/kachakacha/fabrication/Unfold.cpp
    src/next/kachakacha/modeling/SnapEngine.cpp
    src/next/kachakacha/modeling/WorkPlane.cpp
    src/next/kachakacha/modeling/ToolController.cpp
    src/next/kachakacha/modeling/SubshapeKey.cpp
    src/next/kachakacha/modeling/WireCage.cpp
    src/next/kachakacha/io/AtomicFile.cpp
    src/next/kachakacha/io/Json.cpp
    src/next/kachakacha/io/Zip.cpp
    src/next/kachakacha/io/DocumentFile.cpp
    src/next/kachakacha/geometry/ArcBuilders.cpp
    src/next/kachakacha/geometry/CurveSampling.cpp
    src/next/kachakacha/geometry/CurveIntersection.cpp
    src/next/kachakacha/geometry/CurveJoin.cpp
    src/next/kachakacha/geometry/CurveSegment.cpp
    src/next/kachakacha/geometry/Expression.cpp
    src/next/kachakacha/geometry/Measurement.cpp
    src/next/kachakacha/geometry/ScreenMapping.cpp
    src/next/kachakacha/geometry/WireChain.cpp
    src/next/kachakacha/geometry/WireEdit.cpp
)
target_include_directories(kachakacha_v2_core PUBLIC src/next)
target_compile_features(kachakacha_v2_core PUBLIC cxx_std_20)
target_compile_definitions(kachakacha_v2_core
    PUBLIC KACHACAD_V2_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(kachakacha_v2_core PRIVATE /W4 /permissive- /utf-8 /MP)
else()
    target_compile_options(kachakacha_v2_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(kachakacha_v2_occt STATIC
    src/next_occt/kachakacha/kernel/KernelInfo.cpp
    src/next_occt/kachakacha/kernel/OcctCurveConversion.cpp
    src/next_occt/kachakacha/kernel/OcctGuideSurface.cpp
    src/next_occt/kachakacha/kernel/OcctExtrude.cpp
    src/next_occt/kachakacha/kernel/OcctPanelSolid.cpp
    src/next_occt/kachakacha/kernel/OcctWireCage.cpp
    src/next_occt/kachakacha/kernel/OcctShapeCache.cpp
    src/next_occt/kachakacha/kernel/OcctSolidExport.cpp
)
target_include_directories(kachakacha_v2_occt PUBLIC src/next_occt)
target_link_libraries(kachakacha_v2_occt PUBLIC kachakacha_v2_core)
target_compile_features(kachakacha_v2_occt PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(kachakacha_v2_occt PRIVATE /W4 /permissive- /utf-8 /MP)
else()
    target_compile_options(kachakacha_v2_occt PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(KACHACAD_ENABLE_OCCT AND TARGET kachakacha_occt)
    target_compile_definitions(kachakacha_v2_occt PUBLIC KACHACAD_V2_WITH_OCCT)
    # OCCT のヘッダは、この層とその試験だけが見る。core からは見えない(AT-ARC-001)。
    target_include_directories(kachakacha_v2_occt PUBLIC ${KACHACAD_OCCT_INCLUDE_DIR})
    target_link_libraries(kachakacha_v2_occt
        PRIVATE
            TKOffset
            TKBO
            TKMesh
            TKDESTEP
            TKDESTL
    )
endif()

if(KACHACAD_BUILD_TESTS)
    # V2の試験。1件失敗しても残りが走る土台(src/next/kachakacha/base/TestHarness.h)を使う。
    function(kachakacha_add_v2_test name source)
        add_executable(kachakacha_v2_${name} ${source})
        target_link_libraries(kachakacha_v2_${name} PRIVATE kachakacha_v2_core)
        target_compile_definitions(kachakacha_v2_${name}
            PRIVATE KACHACAD_V2_REPO_ROOT="${CMAKE_SOURCE_DIR}")
        kachakacha_apply_test_options(kachakacha_v2_${name})
        add_test(NAME v2_${name} COMMAND kachakacha_v2_${name})
    endfunction()

    kachakacha_add_v2_test(harness_tests tests_v2/harness_tests.cpp)
    kachakacha_add_v2_test(architecture_tests tests_v2/architecture_tests.cpp)
    kachakacha_add_v2_test(scaffold_tests tests_v2/scaffold_tests.cpp)
    kachakacha_add_v2_test(foundation_tests tests_v2/foundation_tests.cpp)
    kachakacha_add_v2_test(curve_tests tests_v2/curve_tests.cpp)
    kachakacha_add_v2_test(expression_tests tests_v2/expression_tests.cpp)
    kachakacha_add_v2_test(wire_chain_tests tests_v2/wire_chain_tests.cpp)
    kachakacha_add_v2_test(arc_builder_tests tests_v2/arc_builder_tests.cpp)
    kachakacha_add_v2_test(wire_edit_tests tests_v2/wire_edit_tests.cpp)
    kachakacha_add_v2_test(curve_sampling_tests tests_v2/curve_sampling_tests.cpp)
    kachakacha_add_v2_test(guide_surface_tests tests_v2/guide_surface_tests.cpp)
    kachakacha_add_v2_test(work_plane_tests tests_v2/work_plane_tests.cpp)
    kachakacha_add_v2_test(extrude_tests tests_v2/extrude_tests.cpp)
    kachakacha_add_v2_test(wire_cage_tests tests_v2/wire_cage_tests.cpp)
    kachakacha_add_v2_test(snap_tests tests_v2/snap_tests.cpp)
    kachakacha_add_v2_test(grid_tests tests_v2/grid_tests.cpp)
    kachakacha_add_v2_test(fabrication_tests tests_v2/fabrication_tests.cpp)
    kachakacha_add_v2_test(opening_clip_tests tests_v2/opening_clip_tests.cpp)
    kachakacha_add_v2_test(assembly_tests tests_v2/assembly_tests.cpp)
    kachakacha_add_v2_test(closed_loop_tests tests_v2/closed_loop_tests.cpp)
    kachakacha_add_v2_test(freeze_state_tests tests_v2/freeze_state_tests.cpp)
    kachakacha_add_v2_test(pattern_tests tests_v2/pattern_tests.cpp)
    kachakacha_add_v2_test(panel_strategy_tests tests_v2/panel_strategy_tests.cpp)
    kachakacha_add_v2_test(export_tests tests_v2/export_tests.cpp)
    kachakacha_add_v2_test(tool_tests tests_v2/tool_tests.cpp)
    kachakacha_add_v2_test(curve_join_tests tests_v2/curve_join_tests.cpp)
    kachakacha_add_v2_test(pdf_tests tests_v2/pdf_tests.cpp)
    kachakacha_add_v2_test(session_tests tests_v2/session_tests.cpp)
    kachakacha_add_v2_test(coverage_tests tests_v2/coverage_tests.cpp)
    kachakacha_add_v2_test(diagnostic_catalog_tests tests_v2/diagnostic_catalog_tests.cpp)
    kachakacha_add_v2_test(command_catalog_tests tests_v2/command_catalog_tests.cpp)
    kachakacha_add_v2_test(operation_guide_tests tests_v2/operation_guide_tests.cpp)
    kachakacha_add_v2_test(ui_mode_tests tests_v2/ui_mode_tests.cpp)
    kachakacha_add_v2_test(view_orientation_tests tests_v2/view_orientation_tests.cpp)
    kachakacha_add_v2_test(guide_surface_table_tests tests_v2/guide_surface_table_tests.cpp)
    kachakacha_add_v2_test(cursor_input_tests tests_v2/cursor_input_tests.cpp)
    kachakacha_add_v2_test(evaluation_queue_tests tests_v2/evaluation_queue_tests.cpp)
    kachakacha_add_v2_test(feature_reevaluation_tests tests_v2/feature_reevaluation_tests.cpp)
    kachakacha_add_v2_test(active_group_tests tests_v2/active_group_tests.cpp)
    kachakacha_add_v2_test(broken_reference_tests tests_v2/broken_reference_tests.cpp)
    kachakacha_add_v2_test(process_step_tests tests_v2/process_step_tests.cpp)
    kachakacha_add_v2_test(export_service_tests tests_v2/export_service_tests.cpp)
    kachakacha_add_v2_test(acceptance_er_tests tests_v2/acceptance_er_tests.cpp)
    kachakacha_add_v2_test(theme_layout_tests tests_v2/theme_layout_tests.cpp)
    kachakacha_add_v2_test(plane_follow_tests tests_v2/plane_follow_tests.cpp)
    kachakacha_add_v2_test(point_source_tests tests_v2/point_source_tests.cpp)
    kachakacha_add_v2_test(robustness_tests tests_v2/robustness_tests.cpp)
    kachakacha_add_v2_test(scale_tests tests_v2/scale_tests.cpp)
    kachakacha_add_v2_test(measurement_tests tests_v2/measurement_tests.cpp)
    kachakacha_add_v2_test(document_tests tests_v2/document_tests.cpp)
    kachakacha_add_v2_test(json_tests tests_v2/json_tests.cpp)
    kachakacha_add_v2_test(zip_tests tests_v2/zip_tests.cpp)
    kachakacha_add_v2_test(document_file_tests tests_v2/document_file_tests.cpp)
    kachakacha_add_v2_test(atomic_file_tests tests_v2/atomic_file_tests.cpp)

    target_link_libraries(kachakacha_v2_scaffold_tests PRIVATE kachakacha_v2_occt)

    # 画面のコードの型検査。Qt の当て木だけで -fsyntax-only を通す。
    # 雲(Qt が入らない)でも、core の関数名の取り違えをここで捕まえる。
    # 本物の組み立てと画面の試験は、Qt のある側で行う。
    if(UNIX)
        add_test(
            NAME v2_qt_stub_typecheck
            COMMAND ${CMAKE_SOURCE_DIR}/tools/qtstub/typecheck.sh
                ${CMAKE_SOURCE_DIR} ${CMAKE_CXX_COMPILER}
        )
        set_tests_properties(v2_qt_stub_typecheck PROPERTIES TIMEOUT 300)
    endif()

    # カーネル層に触る試験。OCCT が無い構成でも「無いと断る」ことを確かめるため常に作る。
    # OCCT 有りのときは、実行時に OCCT のDLLが見えるように PATH を足して走らせる。
    function(kachakacha_add_v2_kernel_test name source)
        add_executable(kachakacha_v2_${name} ${source})
        target_link_libraries(kachakacha_v2_${name}
            PRIVATE
                kachakacha_v2_core
                kachakacha_v2_occt
        )
        target_compile_definitions(kachakacha_v2_${name}
            PRIVATE KACHACAD_V2_REPO_ROOT="${CMAKE_SOURCE_DIR}")
        kachakacha_apply_test_options(kachakacha_v2_${name})
        if(KACHACAD_ENABLE_OCCT AND TARGET kachakacha_occt)
            add_test(
                NAME v2_${name}
                COMMAND ${CMAKE_COMMAND} -E env
                    "PATH=${KACHACAD_OCCT_RUNTIME_PATH}$<SEMICOLON>$ENV{PATH}"
                    $<TARGET_FILE:kachakacha_v2_${name}>
            )
        else()
            add_test(NAME v2_${name} COMMAND kachakacha_v2_${name})
        endif()
        set_tests_properties(v2_${name} PROPERTIES TIMEOUT 900)
    endfunction()

    kachakacha_add_v2_kernel_test(kernel_surface_tests tests_v2/kernel_surface_tests.cpp)
    kachakacha_add_v2_kernel_test(kernel_extrude_tests tests_v2/kernel_extrude_tests.cpp)
    kachakacha_add_v2_kernel_test(kernel_wire_cage_tests tests_v2/kernel_wire_cage_tests.cpp)
    kachakacha_add_v2_kernel_test(kernel_er_export_tests tests_v2/kernel_er_export_tests.cpp)
    kachakacha_add_v2_kernel_test(kernel_export_tests tests_v2/kernel_export_tests.cpp)
endif()

if(KACHACAD_BUILD_QT_APP AND TARGET Qt6::Widgets)
    add_executable(kachakacha_cad_next WIN32
        src/apps/cad_next/main.cpp
        src/apps/cad_next/V2MainWindow.cpp
        src/apps/cad_next/V2MainWindow.h
        src/apps/cad_next/V2Viewport.cpp
        src/apps/cad_next/V2Viewport.h
        src/apps/cad_next/Win95Style.cpp
        src/apps/cad_next/Win95Style.h
    )
    target_link_libraries(kachakacha_cad_next
        PRIVATE
            kachakacha_v2_core
            kachakacha_v2_occt
            Qt6::Widgets
    )
    target_compile_features(kachakacha_cad_next PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(kachakacha_cad_next PRIVATE /W4 /permissive- /utf-8 /MP)
        # --version と --self-test をコンソールから使えるようにする。
        set_target_properties(kachakacha_cad_next PROPERTIES WIN32_EXECUTABLE FALSE)
    else()
        target_compile_options(kachakacha_cad_next PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(KACHACAD_BUILD_TESTS)
        add_test(
            NAME v2_cad_next_smoke
            COMMAND $<TARGET_FILE:kachakacha_cad_next> --self-test
        )
        set_tests_properties(v2_cad_next_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 120
        )
    endif()
endif()
