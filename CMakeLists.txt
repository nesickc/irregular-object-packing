cmake_minimum_required(VERSION 4.2)

project(
    irregular_object_packing
    VERSION 0.0.0
    DESCRIPTION "Windows-native irregular object packing"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(IropTooling)
include(IropWarnings)

option(IROP_WARNINGS_AS_ERRORS "Treat warnings in project-owned C++ code as errors" ON)
option(IROP_ENABLE_CLANG_TIDY "Run the focused clang-tidy profile while compiling" OFF)

# CMake only integrates <LANG>_CLANG_TIDY with Makefile and Ninja generators.
# Keep analysis opt-in so the supported Visual Studio generator remains usable.
irop_configure_clang_tidy()

find_package(CLI11 CONFIG REQUIRED)
find_package(Eigen3 3.4.1 EXACT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(TetGen 1.6.0 EXACT CONFIG REQUIRED)
find_package(
    VTK 9.3
    CONFIG
    REQUIRED
    COMPONENTS
        CommonCore
        CommonDataModel
        CommonMisc
        IOGeometry
        IOXML
)

set(
    IROP_CORE_SOURCES
    src/error.cpp
    src/cat/cat.cpp
    src/geometry/mesh_geometry.cpp
    src/geometry/sampling_policy.cpp
    src/geometry/transform.cpp
    src/initialization/initialize_scene.cpp
    src/inspection/inspect_stl.cpp
    src/io/stl_io.cpp
    src/io/diagnostic_geometry.cpp
    src/model/mesh_validation.cpp
    src/packing/initialization.cpp
    src/tetrahedralization/tetgen_adapter.cpp
)
set(
    IROP_PUBLIC_HEADERS
    include/irop/error.hpp
    include/irop/cat/cat.hpp
    include/irop/geometry/mesh_geometry.hpp
    include/irop/geometry/sampling_policy.hpp
    include/irop/geometry/transform.hpp
    include/irop/initialization/initialize_scene.hpp
    include/irop/inspection/inspect_stl.hpp
    include/irop/io/stl_io.hpp
    include/irop/io/diagnostic_geometry.hpp
    include/irop/model/mesh_validation.hpp
    include/irop/model/triangle_mesh.hpp
    include/irop/packing/initialization.hpp
    include/irop/tetrahedralization/tetrahedralization.hpp
)
set(
    IROP_VTK_TARGETS
    VTK::CommonCore
    VTK::CommonDataModel
    VTK::CommonMisc
    VTK::IOGeometry
    VTK::IOXML
)

# Static is intentional until the project defines an installed ABI and symbol
# export contract. Dependency-specific types remain private to this target.
add_library(irop_core STATIC ${IROP_CORE_SOURCES} ${IROP_PUBLIC_HEADERS})
add_library(irop::core ALIAS irop_core)
target_compile_features(irop_core PUBLIC cxx_std_20)
set_target_properties(irop_core PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(irop_core PRIVATE EIGEN_MPL2_ONLY IROP_VERSION="${PROJECT_VERSION}")
target_include_directories(
    irop_core
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
    irop_core
    PRIVATE
        Eigen3::Eigen
        nlohmann_json::nlohmann_json
        TetGen::TetGen
        ${IROP_VTK_TARGETS}
)
irop_enable_project_warnings(irop_core)

add_executable(irop app/irop/main.cpp)
if(WIN32)
    target_sources(irop PRIVATE app/irop/irop.manifest)
endif()
target_compile_features(irop PRIVATE cxx_std_20)
set_target_properties(irop PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(irop PRIVATE IROP_VERSION="${PROJECT_VERSION}")
target_link_libraries(
    irop
    PRIVATE
        irop::core
        CLI11::CLI11
        spdlog::spdlog
)
irop_enable_project_warnings(irop)
vtk_module_autoinit(TARGETS irop_core irop MODULES ${IROP_VTK_TARGETS})

if(BUILD_TESTING)
    add_subdirectory(tests/cpp)
    vtk_module_autoinit(TARGETS irop_tests MODULES ${IROP_VTK_TARGETS})
endif()

file(
    GLOB_RECURSE IROP_FORMAT_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpp/*.hpp"
)
irop_add_clang_format_targets(FILES ${IROP_FORMAT_FILES})
