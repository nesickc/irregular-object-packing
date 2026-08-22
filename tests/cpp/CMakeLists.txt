find_package(Catch2 3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(
    irop_tests
    cat_tests.cpp
    diagnostic_geometry_tests.cpp
    initialization_scene_tests.cpp
    initialization_tests.cpp
    inspection_tests.cpp
    mesh_geometry_tests.cpp
    mesh_validation_tests.cpp
    sampling_policy_tests.cpp
    stl_io_tests.cpp
    tetrahedralization_tests.cpp
    transform_tests.cpp
)
target_compile_features(irop_tests PRIVATE cxx_std_20)
set_target_properties(irop_tests PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(
    irop_tests
    PRIVATE
        IROP_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
        IROP_TEST_SCHEMA_DIR="${PROJECT_SOURCE_DIR}/docs/schemas"
)
target_link_libraries(
    irop_tests
    PRIVATE
        Catch2::Catch2WithMain
        irop_core
        nlohmann_json::nlohmann_json
        VTK::IOXML
)

if(COMMAND irop_enable_project_warnings)
    irop_enable_project_warnings(irop_tests)
endif()

include(Catch)
catch_discover_tests(irop_tests)

add_test(
    NAME irop_cli_smoke
    COMMAND
        "${CMAKE_COMMAND}"
        "-DIROP_EXECUTABLE=$<TARGET_FILE:irop>"
        "-DIROP_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/tetra_ascii.stl"
        "-DIROP_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-smoke"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake"
)
set_tests_properties(irop_cli_smoke PROPERTIES LABELS "cli;smoke")

add_test(
    NAME irop_initialization_cli_smoke
    COMMAND
        "${CMAKE_COMMAND}"
        "-DIROP_EXECUTABLE=$<TARGET_FILE:irop>"
        "-DIROP_OBJECT_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/tetra_ascii.stl"
        "-DIROP_CONTAINER_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/cube_ascii.stl"
        "-DIROP_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/initialization-cli-smoke"
        -P
        "${CMAKE_CURRENT_SOURCE_DIR}/initialization_cli_smoke.cmake"
)
set_tests_properties(irop_initialization_cli_smoke PROPERTIES LABELS "cli;initialization;smoke")
