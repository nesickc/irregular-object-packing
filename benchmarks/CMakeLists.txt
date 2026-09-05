add_executable(irop_benchmarks main.cpp support.hpp)
target_compile_features(irop_benchmarks PRIVATE cxx_std_20)
set_target_properties(irop_benchmarks PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(irop_benchmarks PRIVATE irop_core nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(irop_benchmarks PRIVATE Psapi)
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