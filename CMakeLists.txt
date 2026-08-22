cmake_minimum_required(VERSION 3.20)

project(kachakachaCAD
    VERSION 0.1.0
    DESCRIPTION "Wireframe and sheet-material CAD for railway model making"
    LANGUAGES CXX
)

option(KACHACAD_BUILD_TESTS "Build kachakachaCAD tests" ON)
option(KACHACAD_BUILD_QT_APP "Build the Qt desktop application when Qt 6 is available" ON)

if(KACHACAD_BUILD_TESTS)
    enable_testing()
endif()

add_library(kachakacha_core STATIC
    src/core/kachakacha/geometry/Vector3.cpp
    src/core/kachakacha/io/PlateFlatPattern.cpp
    src/core/kachakacha/io/PlanarExport.cpp
    src/core/kachakacha/io/ProjectScript.cpp
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

    if(KACHACAD_BUILD_QT_APP)
        if(EXISTS "C:/Qt/6.9.2/msvc2022_64" AND NOT DEFINED Qt6_DIR)
            list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.9.2/msvc2022_64")
        endif()

        find_package(Qt6 6.5 COMPONENTS Widgets QUIET)
        if(Qt6_FOUND)
            add_executable(kachakacha_cad WIN32
                src/apps/cad/CadViewport.cpp
                src/apps/cad/CadViewport.h
                src/apps/cad/MainWindow.cpp
                src/apps/cad/MainWindow.h
                src/apps/cad/main.cpp
            )

            target_link_libraries(kachakacha_cad
                PRIVATE
                    kachakacha_core
                    Qt6::Widgets
            )

            target_compile_features(kachakacha_cad PRIVATE cxx_std_20)
            if(MSVC)
                target_compile_options(kachakacha_cad PRIVATE /W4 /permissive- /utf-8)
            else()
                target_compile_options(kachakacha_cad PRIVATE -Wall -Wextra -Wpedantic)
            endif()

            qt_finalize_executable(kachakacha_cad)
            message(STATUS "Qt desktop application enabled: ${Qt6_VERSION}")

            if(KACHACAD_BUILD_TESTS)
                get_filename_component(KACHACAD_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
                add_test(
                    NAME qt_cad_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        "QT_QPA_PLATFORM=offscreen"
                        "QT_PLUGIN_PATH=${KACHACAD_QT_PREFIX}/plugins"
                        "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>$ENV{PATH}"
                        $<TARGET_FILE:kachakacha_cad>
                        --self-test
                        --project "${CMAKE_SOURCE_DIR}/examples/first-check.kcd"
                )
                add_test(
                    NAME qt_surface_sample_smoke
                    COMMAND ${CMAKE_COMMAND} -E env
                        "QT_QPA_PLATFORM=offscreen"
                        "QT_PLUGIN_PATH=${KACHACAD_QT_PREFIX}/plugins"
                        "PATH=$<TARGET_FILE_DIR:Qt6::Core>$<SEMICOLON>$ENV{PATH}"
                        $<TARGET_FILE:kachakacha_cad>
                        --project "${CMAKE_SOURCE_DIR}/examples/curved-panel-light-holes.kcd"
                        --snapshot "${CMAKE_CURRENT_BINARY_DIR}/surface-sample-smoke.png"
                )
            endif()
        else()
            message(WARNING "Qt 6.5 or newer was not found; kachakacha_cad will not be built.")
        endif()
    endif()
endif()

if(KACHACAD_BUILD_TESTS)
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
