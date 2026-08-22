foreach(required_variable IN ITEMS IROP_EXECUTABLE IROP_OBJECT_FIXTURE IROP_CONTAINER_FIXTURE IROP_WORK_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be provided to the initialization CLI smoke test")
    endif()
endforeach()

function(run_and_expect expected_exit description)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE actual_exit
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
        TIMEOUT 30
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
set(valid_output "${IROP_WORK_DIR}/initialized-tętra-网")
file(REMOVE_RECURSE "${valid_output}")

run_and_expect(0 "initialize help" "${IROP_EXECUTABLE}" initialize --help)
run_and_expect(2 "missing initialize arguments" "${IROP_EXECUTABLE}" initialize)
run_and_expect(
    3
    "invalid initial scale"
    "${IROP_EXECUTABLE}"
    initialize
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    2
    --initial-volume-scale
    0
    --output-dir
    "${IROP_WORK_DIR}/invalid-scale"
)
run_and_expect(
    4
    "generated triangle limit"
    "${IROP_EXECUTABLE}"
    initialize
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    2
    --max-output-triangles
    7
    --output-dir
    "${IROP_WORK_DIR}/output-limit"
)
set(work_limit_output "${IROP_WORK_DIR}/geometry-work-limit")
file(REMOVE_RECURSE "${work_limit_output}")
run_and_expect(
    4
    "geometry query work limit"
    "${IROP_EXECUTABLE}"
    initialize
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    2
    --max-geometry-query-triangle-visits
    11
    --output-dir
    "${work_limit_output}"
)
if(EXISTS "${work_limit_output}")
    message(FATAL_ERROR "resource-limited initialization published an output directory")
endif()
run_and_expect(
    0
    "valid initialization"
    "${IROP_EXECUTABLE}"
    initialize
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    3
    --seed
    123
    --write-individual-stls
    --output-dir
    "${valid_output}"
)

set(combined_stl "${valid_output}/initialized-objects.stl")
set(container_stl "${valid_output}/container.stl")
set(placements_path "${valid_output}/placements.json")
set(run_summary_path "${valid_output}/run-summary.json")
foreach(required_artifact IN ITEMS "${combined_stl}" "${container_stl}" "${placements_path}" "${run_summary_path}")
    if(NOT EXISTS "${required_artifact}" OR IS_DIRECTORY "${required_artifact}")
        message(FATAL_ERROR "valid initialization did not produce ${required_artifact}")
    endif()
endforeach()
foreach(object_index IN ITEMS 000000 000001 000002)
    set(object_path "${valid_output}/objects/object-${object_index}.stl")
    if(NOT EXISTS "${object_path}" OR IS_DIRECTORY "${object_path}")
        message(FATAL_ERROR "valid initialization did not produce ${object_path}")
    endif()
endforeach()

file(SIZE "${combined_stl}" combined_size)
if(NOT combined_size EQUAL 684)
    message(FATAL_ERROR "combined initialized STL has unexpected size ${combined_size}")
endif()
file(READ "${placements_path}" placements_json)
string(JSON schema_version ERROR_VARIABLE schema_error GET "${placements_json}" schema_version)
string(JSON command ERROR_VARIABLE command_error GET "${placements_json}" command)
string(JSON placement_count ERROR_VARIABLE placements_error LENGTH "${placements_json}" placements)
string(JSON rotation_order ERROR_VARIABLE order_error GET "${placements_json}" transform_convention rotation_order)
if(schema_error OR command_error OR placements_error OR order_error)
    message(FATAL_ERROR "placements JSON could not be inspected")
endif()
if(NOT schema_version EQUAL 1 OR NOT command STREQUAL "initialize" OR NOT placement_count EQUAL 3 OR
   NOT rotation_order STREQUAL "Ry*Rz*Rx")
    message(FATAL_ERROR "placements JSON has unexpected initialization content")
endif()

file(READ "${run_summary_path}" run_summary_json)
string(JSON summary_schema ERROR_VARIABLE summary_schema_error GET "${run_summary_json}" schema_version)
string(JSON outcome ERROR_VARIABLE outcome_error GET "${run_summary_json}" outcome category)
string(JSON object_count ERROR_VARIABLE count_error GET "${run_summary_json}" config object_count)
string(JSON individual_count ERROR_VARIABLE individual_error LENGTH "${run_summary_json}" outputs individual_object_stls)
if(summary_schema_error OR outcome_error OR count_error OR individual_error)
    message(FATAL_ERROR "run summary JSON could not be inspected")
endif()
if(NOT summary_schema EQUAL 1 OR NOT outcome STREQUAL "success" OR NOT object_count EQUAL 3 OR
   NOT individual_count EQUAL 3)
    message(FATAL_ERROR "run summary JSON has unexpected initialization content")
endif()

run_and_expect(
    5
    "initialization artifact collision"
    "${IROP_EXECUTABLE}"
    initialize
    --object
    "${IROP_OBJECT_FIXTURE}"
    --container
    "${IROP_CONTAINER_FIXTURE}"
    --count
    3
    --seed
    123
    --output-dir
    "${valid_output}"
)
