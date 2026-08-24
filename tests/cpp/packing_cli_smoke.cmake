foreach(
    required_variable
    IN ITEMS IROP_EXECUTABLE IROP_OBJECT_FIXTURE IROP_CONTAINER_FIXTURE IROP_SCHEMA_DIR IROP_WORK_DIR
)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be provided to the packing CLI smoke test")
    endif()
endforeach()
foreach(schema_name IN ITEMS packing-placements-v1.schema.json packing-run-summary-v1.schema.json)
    if(NOT EXISTS "${IROP_SCHEMA_DIR}/${schema_name}")
        message(FATAL_ERROR "packing CLI smoke test cannot find ${schema_name}")
    endif()
endforeach()

function(run_and_expect expected_exit description)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
        TIMEOUT 120
    )
    if(NOT "${actual_exit}" MATCHES "^-?[0-9]+$")
        message(
            FATAL_ERROR
            "${description}: process did not return a numeric exit code: ${actual_exit}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}"
        )
    endif()
    if(NOT actual_exit EQUAL expected_exit)
        message(
            FATAL_ERROR
            "${description}: expected exit ${expected_exit}, got ${actual_exit}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}"
        )
    endif()
endfunction()

file(MAKE_DIRECTORY "${IROP_WORK_DIR}")
set(success_output "${IROP_WORK_DIR}/packed-tętra-网")
set(failure_output "${IROP_WORK_DIR}/bounded-failure")
set(infeasible_output "${IROP_WORK_DIR}/infeasible")
set(invalid_output "${IROP_WORK_DIR}/invalid-config")
file(REMOVE_RECURSE "${success_output}" "${failure_output}" "${infeasible_output}" "${invalid_output}")

run_and_expect(0 "pack help" "${IROP_EXECUTABLE}" pack --help)
run_and_expect(2 "missing pack arguments" "${IROP_EXECUTABLE}" pack)
run_and_expect(
    3
    "invalid final scale"
    "${IROP_EXECUTABLE}"
    pack
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    1
    --final-volume-scale
    0
    --output-dir
    "${invalid_output}"
)
if(EXISTS "${invalid_output}")
    message(FATAL_ERROR "invalid packing configuration published an output directory")
endif()

run_and_expect(
    0
    "successful bounded packing"
    "${IROP_EXECUTABLE}"
    pack
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    1
    --initial-volume-scale
    0.1
    --final-volume-scale
    0.1001
    --scale-steps
    1
    --max-iterations-per-scale-step
    3
    --no-adaptive-sampling
    --output-dir
    "${success_output}"
)

foreach(required_artifact IN ITEMS packed-objects.stl container.stl placements.json run-summary.json)
    if(NOT EXISTS "${success_output}/${required_artifact}" OR IS_DIRECTORY "${success_output}/${required_artifact}")
        message(FATAL_ERROR "successful packing did not produce ${required_artifact}")
    endif()
endforeach()
file(GLOB success_artifacts RELATIVE "${success_output}" "${success_output}/*")
list(SORT success_artifacts)
set(expected_success_artifacts container.stl packed-objects.stl placements.json run-summary.json)
if(NOT success_artifacts STREQUAL expected_success_artifacts)
    message(FATAL_ERROR "successful packing published an unexpected artifact set: ${success_artifacts}")
endif()

file(READ "${success_output}/placements.json" placements_json)
string(JSON placements_command ERROR_VARIABLE placements_error GET "${placements_json}" command)
string(JSON placements_phase ERROR_VARIABLE phase_error GET "${placements_json}" phase)
string(JSON placement_count ERROR_VARIABLE count_error LENGTH "${placements_json}" placements)
if(placements_error OR phase_error OR count_error OR NOT placements_command STREQUAL "pack" OR
   NOT placements_phase STREQUAL "final" OR NOT placement_count EQUAL 1)
    message(FATAL_ERROR "successful packing placements JSON has unexpected content")
endif()

file(READ "${success_output}/run-summary.json" success_summary)
string(JSON success_outcome ERROR_VARIABLE outcome_error GET "${success_summary}" outcome category)
string(JSON physical_valid ERROR_VARIABLE validation_error GET "${success_summary}" validation physical_scene_valid)
string(JSON packed_path ERROR_VARIABLE path_error GET "${success_summary}" outputs packed_objects_stl)
if(outcome_error OR validation_error OR path_error OR NOT success_outcome STREQUAL "success" OR
   NOT physical_valid OR NOT packed_path STREQUAL "packed-objects.stl")
    message(FATAL_ERROR "successful packing run summary has unexpected content")
endif()

run_and_expect(
    4
    "bounded local-solve exhaustion"
    "${IROP_EXECUTABLE}"
    pack
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    2
    --initial-volume-scale
    0.1
    --final-volume-scale
    0.2
    --scale-steps
    1
    --max-total-local-solves
    1
    --no-adaptive-sampling
    --output-dir
    "${failure_output}"
)
file(GLOB failure_artifacts RELATIVE "${failure_output}" "${failure_output}/*")
if(NOT failure_artifacts STREQUAL "run-summary.json")
    message(FATAL_ERROR "unsuccessful packing published misleading artifacts: ${failure_artifacts}")
endif()
file(READ "${failure_output}/run-summary.json" failure_summary)
string(JSON failure_outcome ERROR_VARIABLE failure_error GET "${failure_summary}" outcome category)
string(JSON failure_packed_path ERROR_VARIABLE failure_path_error GET "${failure_summary}" outputs packed_objects_stl)
if(failure_error OR failure_path_error OR NOT failure_outcome STREQUAL "resource_exhausted" OR
   NOT failure_packed_path STREQUAL "")
    message(FATAL_ERROR "unsuccessful packing run summary has unexpected content")
endif()

run_and_expect(
    6
    "deterministic infeasible packing"
    "${IROP_EXECUTABLE}"
    pack
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    1
    --initial-volume-scale
    0.1
    --final-volume-scale
    0.1001
    --scale-steps
    1
    --max-iterations-per-scale-step
    1
    --padding
    100
    --no-adaptive-sampling
    --output-dir
    "${infeasible_output}"
)
file(GLOB infeasible_artifacts RELATIVE "${infeasible_output}" "${infeasible_output}/*")
if(NOT infeasible_artifacts STREQUAL "run-summary.json")
    message(FATAL_ERROR "infeasible packing published misleading artifacts: ${infeasible_artifacts}")
endif()
file(READ "${infeasible_output}/run-summary.json" infeasible_summary)
string(JSON infeasible_outcome ERROR_VARIABLE infeasible_error GET "${infeasible_summary}" outcome category)
if(infeasible_error OR NOT infeasible_outcome STREQUAL "infeasible")
    message(FATAL_ERROR "infeasible packing run summary has unexpected content")
endif()
