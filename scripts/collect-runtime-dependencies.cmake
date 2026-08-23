cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

foreach(required_variable IN ITEMS
    KACHACAD_EXECUTABLE
    KACHACAD_OUTPUT_DIRECTORY
    KACHACAD_VCPKG_RUNTIME
    KACHACAD_QT_RUNTIME)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${KACHACAD_EXECUTABLE}"
    DIRECTORIES
        "${KACHACAD_OUTPUT_DIRECTORY}"
        "${KACHACAD_VCPKG_RUNTIME}"
        "${KACHACAD_QT_RUNTIME}"
    RESOLVED_DEPENDENCIES_VAR resolved_dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved_dependencies
    PRE_EXCLUDE_REGEXES
        "api-ms-win-.*"
        "ext-ms-.*"
        "HvsiFileTrust\\.dll"
        "PdmUtilities\\.dll"
    POST_EXCLUDE_REGEXES
        ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
)

if(unresolved_dependencies)
    list(JOIN unresolved_dependencies ", " unresolved_list)
    message(FATAL_ERROR "Unresolved runtime dependencies: ${unresolved_list}")
endif()

cmake_path(NORMAL_PATH KACHACAD_VCPKG_RUNTIME OUTPUT_VARIABLE normalized_vcpkg_runtime)
foreach(dependency IN LISTS resolved_dependencies)
    cmake_path(GET dependency PARENT_PATH dependency_directory)
    cmake_path(NORMAL_PATH dependency_directory OUTPUT_VARIABLE normalized_dependency_directory)
    if(normalized_dependency_directory STREQUAL normalized_vcpkg_runtime)
        file(COPY "${dependency}" DESTINATION "${KACHACAD_OUTPUT_DIRECTORY}")
    endif()
endforeach()
