cmake_minimum_required(VERSION 3.20)

project(kachakachaCAD
    VERSION 0.1.0
    DESCRIPTION "Wireframe and sheet-material CAD for railway model making"
    LANGUAGES CXX
)

option(KACHACAD_BUILD_TESTS "Build kachakachaCAD tests" ON)

add_library(kachakacha_core STATIC
    src/core/kachakacha/geometry/Vector3.cpp
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

if(KACHACAD_BUILD_TESTS)
    enable_testing()

    add_executable(kachakacha_core_tests
        tests/workplane_tests.cpp
    )

    target_link_libraries(kachakacha_core_tests
        PRIVATE
            kachakacha_core
    )

    add_test(NAME workplane_tests COMMAND kachakacha_core_tests)
endif()

