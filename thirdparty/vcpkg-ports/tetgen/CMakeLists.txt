cmake_minimum_required(VERSION 3.20)

project(TetGen VERSION 1.6.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(NOT IS_DIRECTORY "${TETGEN_SOURCE_DIR}")
    message(FATAL_ERROR "TETGEN_SOURCE_DIR must name the pinned TetGen source directory")
endif()

add_library(
    TetGen
    STATIC
    "${TETGEN_SOURCE_DIR}/predicates.cxx"
    "${TETGEN_SOURCE_DIR}/tetgen.cxx"
)
add_library(TetGen::TetGen ALIAS TetGen)

# TetGen's public header changes declarations and inline failure behavior when
# used as a library, so consumers must compile with the same definition.
target_compile_definitions(TetGen PUBLIC TETLIBRARY)
target_include_directories(
    TetGen
    PUBLIC
        "$<BUILD_INTERFACE:${TETGEN_SOURCE_DIR}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(
    TetGen
    PROPERTIES
        OUTPUT_NAME tetgen
        POSITION_INDEPENDENT_CODE ON
)

install(
    TARGETS TetGen
    EXPORT TetGenTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES "${TETGEN_SOURCE_DIR}/tetgen.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
    EXPORT TetGenTargets
    FILE TetGenTargets.cmake
    NAMESPACE TetGen::
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/tetgen"
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/TetGenConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TetGenConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/tetgen"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TetGenConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/TetGenConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/TetGenConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/tetgen"
)
