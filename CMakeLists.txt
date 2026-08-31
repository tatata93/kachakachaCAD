cmake_minimum_required(VERSION 3.20)

project(kachakachaCAD
    VERSION 0.1.0
    DESCRIPTION "Wireframe and sheet-material CAD for railway model making"
    LANGUAGES CXX
)

option(KACHACAD_BUILD_TESTS "Build kachakachaCAD tests" ON)
option(KACHACAD_BUILD_QT_APP "Build the Qt desktop application when Qt 6 is available" ON)
option(KACHACAD_ENABLE_OCCT "Enable Open CASCADE B-Rep and 3D export support" ON)

if(KACHACAD_BUILD_TESTS)
    enable_testing()
endif()

add_library(kachakacha_core STATIC
    src/core/kachakacha/geometry/Vector3.cpp
    src/core/kachakacha/io/NumericExpression.cpp
    src/core/kachakacha/io/PlateFlatPattern.cpp
    src/core/kachakacha/io/PlanarExport.cpp
    src/core/kachakacha/io/ProjectScript.cpp
    src/core/kachakacha/model/Body.cpp
    src/core/kachakacha/model/Measurement.cpp
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
    target_compile_options(kachakacha_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(kachakacha_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(KACHACAD_ENABLE_OCCT)
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
        if(TARGET TKernel AND TARGET TKOffset AND TARGET TKMesh
            AND TARGET TKDESTEP AND TARGET TKDESTL)
            set(OpenCASCADE_FOUND TRUE)
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
            target_compile_options(kachakacha_occt PRIVATE /W4 /permissive- /utf-8)
        else()
            target_compile_options(kachakacha_occt PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        set(KACHACAD_OCCT_RUNTIME_PATH "$<TARGET_FILE_DIR:TKernel>")
        if(EXISTS "$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin")
            string(APPEND KACHACAD_OCCT_RUNTIME_PATH
                "$<SEMICOLON>$ENV{USERPROFILE}/vcpkg/installed/x64-windows/debug/bin"
                "$<SEMICOLON>$ENV{USERPROFILE}/vcpkg/installed/x64-windows/bin")
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
endif()

# The Qt desktop application is cross-platform: build it whenever Qt 6 and
# Open CASCADE are available, not only on Windows.
if(KACHACAD_BUILD_QT_APP)
        if(EXISTS "C:/Qt/6.9.2/msvc2022_64" AND NOT DEFINED Qt6_DIR)
            list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.9.2/msvc2022_64")
        endif()

        find_package(Qt6 6.5 COMPONENTS Widgets QUIET)
        if(Qt6_FOUND AND TARGET kachakacha_occt)
            add_executable(kachakacha_cad WIN32
                src/apps/cad/CadViewport.cpp
                src/apps/cad/CadViewport.h
                src/apps/cad/MainWindow.cpp
                src/apps/cad/MainWindow.h
                src/apps/cad/MainWindowSelfTest.cpp
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
                target_compile_options(kachakacha_cad PRIVATE /W4 /permissive- /utf-8)
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

                add_executable(kachakacha_plate_pdf_tests
                    src/apps/cad/PlatePdfExport.cpp
                    src/apps/cad/PlatePdfExport.h
                    tests/plate_pdf_tests.cpp
                )
                target_include_directories(kachakacha_plate_pdf_tests PRIVATE src/apps/cad)
                target_link_libraries(kachakacha_plate_pdf_tests PRIVATE kachakacha_core Qt6::Gui)
                target_compile_features(kachakacha_plate_pdf_tests PRIVATE cxx_std_20)
                if(MSVC)
                    target_compile_options(kachakacha_plate_pdf_tests PRIVATE /W4 /permissive- /utf-8)
                endif()
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

    add_test(NAME numeric_expression_tests COMMAND kachakacha_numeric_expression_tests)

    add_executable(kachakacha_workplane_tests
        tests/workplane_tests.cpp
    )

    target_link_libraries(kachakacha_workplane_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME workplane_tests COMMAND kachakacha_workplane_tests)

    add_executable(kachakacha_sketch_wire_tests
        tests/sketch_wire_tests.cpp
    )

    target_link_libraries(kachakacha_sketch_wire_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME sketch_wire_tests COMMAND kachakacha_sketch_wire_tests)

    add_executable(kachakacha_project_script_tests
        tests/project_script_tests.cpp
    )

    target_link_libraries(kachakacha_project_script_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME project_script_tests COMMAND kachakacha_project_script_tests)

    add_executable(kachakacha_wire_operations_tests
        tests/wire_operations_tests.cpp
    )

    target_link_libraries(kachakacha_wire_operations_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME wire_operations_tests COMMAND kachakacha_wire_operations_tests)

    add_executable(kachakacha_measurement_tests
        tests/measurement_tests.cpp
    )

    target_link_libraries(kachakacha_measurement_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME measurement_tests COMMAND kachakacha_measurement_tests)

    add_executable(kachakacha_planar_export_tests
        tests/planar_export_tests.cpp
    )

    target_link_libraries(kachakacha_planar_export_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME planar_export_tests COMMAND kachakacha_planar_export_tests)

    add_executable(kachakacha_plate_flat_pattern_tests
        tests/plate_flat_pattern_tests.cpp
    )

    target_link_libraries(kachakacha_plate_flat_pattern_tests
        PRIVATE
            kachakacha_core
    )

    add_test(
        NAME plate_flat_pattern_tests
        COMMAND kachakacha_plate_flat_pattern_tests
            "${CMAKE_SOURCE_DIR}/examples/curved-panel-light-holes.kcd"
            "${CMAKE_SOURCE_DIR}/examples/railway-nose-acceptance.kcd"
            "${CMAKE_CURRENT_BINARY_DIR}/railway-nose-papercraft.svg"
    )

    add_executable(kachakacha_surface_tests
        tests/surface_tests.cpp
    )

    target_link_libraries(kachakacha_surface_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME surface_tests COMMAND kachakacha_surface_tests)

    add_executable(kachakacha_plate_tests
        tests/plate_tests.cpp
    )

    target_link_libraries(kachakacha_plate_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME plate_tests COMMAND kachakacha_plate_tests)

    add_executable(kachakacha_body_tests
        tests/body_tests.cpp
    )

    target_link_libraries(kachakacha_body_tests
        PRIVATE
            kachakacha_core
    )

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
