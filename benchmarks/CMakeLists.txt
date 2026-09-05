add_executable(irop_benchmarks main.cpp support.hpp)
target_compile_features(irop_benchmarks PRIVATE cxx_std_20)
set_target_properties(irop_benchmarks PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(irop_benchmarks PRIVATE irop_core nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(irop_benchmarks PRIVATE Psapi Bcrypt)
endif()
target_compile_definitions(
    irop_benchmarks PRIVATE
    IROP_BENCHMARK_BUILD_CONFIG="$<CONFIG>"
    IROP_BENCHMARK_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    IROP_BENCHMARK_VTK_VERSION="${VTK_VERSION}"
)
find_package(Git QUIET)
set(IROP_BENCHMARK_REVISION "unavailable")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE IROP_BENCHMARK_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
file(SHA256 "${PROJECT_SOURCE_DIR}/src/packing/initialization.cpp" IROP_BENCHMARK_INITIALIZER_SHA256)
file(SHA256 "${PROJECT_SOURCE_DIR}/src/geometry/collision.cpp" IROP_BENCHMARK_COLLISION_SHA256)
target_compile_definitions(
    irop_benchmarks PRIVATE
    IROP_BENCHMARK_REVISION="${IROP_BENCHMARK_REVISION}"
    IROP_BENCHMARK_INITIALIZER_SHA256="${IROP_BENCHMARK_INITIALIZER_SHA256}"
    IROP_BENCHMARK_COLLISION_SHA256="${IROP_BENCHMARK_COLLISION_SHA256}"
)
irop_enable_project_warnings(irop_benchmarks)
vtk_module_autoinit(TARGETS irop_benchmarks MODULES ${IROP_VTK_TARGETS})

set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/packing/initialization.cpp"
    "${PROJECT_SOURCE_DIR}/src/geometry/collision.cpp"
)
# Embed hashes of every project-owned measured module and its build inputs. The
# dependency list causes normal builds to reconfigure after any measured edit.
file(GLOB_RECURSE IROP_BENCHMARK_MEASURED_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
    "${PROJECT_SOURCE_DIR}/benchmarks/*.cpp" "${PROJECT_SOURCE_DIR}/benchmarks/*.hpp"
    "${PROJECT_SOURCE_DIR}/cmake/*.cmake"
)
list(APPEND IROP_BENCHMARK_MEASURED_SOURCES
    "${PROJECT_SOURCE_DIR}/CMakeLists.txt"
    "${PROJECT_SOURCE_DIR}/CMakePresets.json"
    "${PROJECT_SOURCE_DIR}/vcpkg.json"
    "${PROJECT_SOURCE_DIR}/vcpkg-configuration.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-stl-windows.ps1"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-windows.ps1"
)
list(SORT IROP_BENCHMARK_MEASURED_SOURCES)
set(IROP_BENCHMARK_SOURCE_JSON "{}")
foreach(IROP_MEASURED_SOURCE IN LISTS IROP_BENCHMARK_MEASURED_SOURCES)
    file(SHA256 "${IROP_MEASURED_SOURCE}" IROP_MEASURED_SHA256)
    file(RELATIVE_PATH IROP_MEASURED_RELATIVE "${PROJECT_SOURCE_DIR}" "${IROP_MEASURED_SOURCE}")
    string(JSON IROP_BENCHMARK_SOURCE_JSON SET "${IROP_BENCHMARK_SOURCE_JSON}"
        "${IROP_MEASURED_RELATIVE}" "\"${IROP_MEASURED_SHA256}\"")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/source_hashes.hpp"
    "#pragma once\nnamespace irop::benchmark { inline constexpr const char* source_hashes = R\"irop(${IROP_BENCHMARK_SOURCE_JSON})irop\"; }\n")
target_include_directories(irop_benchmarks PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${IROP_BENCHMARK_MEASURED_SOURCES})

if(BUILD_TESTING)
    add_test(NAME irop_benchmark_pack_smoke COMMAND "${CMAKE_COMMAND}"
        "-DIROP_BENCHMARK_EXECUTABLE=$<TARGET_FILE:irop_benchmarks>"
        "-DIROP_OBJECT_FIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/tetra_ascii.stl"
        "-DIROP_CONTAINER_FIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/cube_ascii.stl"
        "-DIROP_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/pack-smoke-$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/smoke.cmake")
    set_tests_properties(irop_benchmark_pack_smoke PROPERTIES LABELS "benchmark;packing;smoke" TIMEOUT 90)
endif()
