cmake_minimum_required(VERSION 3.20)

project(kachakachaCAD
    VERSION 0.1.0
    DESCRIPTION "Wireframe and sheet-material CAD for railway model making"
    LANGUAGES CXX
)

option(KACHACAD_BUILD_TESTS "Build kachakachaCAD tests" ON)

add_library(kachakacha_core STATIC
    src/core/kachakacha/geometry/Vector3.cpp
    src/core/kachakacha/model/Sketch.cpp
    src/core/kachakacha/model/Wire.cpp
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
endif()

if(KACHACAD_BUILD_TESTS)
    enable_testing()

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

    if(WIN32)
        add_test(
            NAME viewer_snapshot
            COMMAND $<TARGET_FILE:kachakacha_viewer> --snapshot "${CMAKE_BINARY_DIR}/viewer-smoke.bmp"
        )
    endif()
endif()
