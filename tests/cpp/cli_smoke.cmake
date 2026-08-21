foreach(required_variable IN ITEMS IROP_EXECUTABLE IROP_FIXTURE IROP_WORK_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be provided to the CLI smoke test")
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
set(valid_output "${IROP_WORK_DIR}/valid-output-tętra-网")
file(MAKE_DIRECTORY "${valid_output}")
file(REMOVE
    "${valid_output}/normalized.stl"
    "${valid_output}/inspection-summary.json"
    "${valid_output}/.normalized.stl.irop-tmp"
    "${valid_output}/.inspection-summary.json.irop-tmp"
)

run_and_expect(0 "help" "${IROP_EXECUTABLE}" --help)
run_and_expect(0 "version" "${IROP_EXECUTABLE}" --version)
run_and_expect(2 "missing subcommand" "${IROP_EXECUTABLE}")
run_and_expect(2 "missing inspect arguments" "${IROP_EXECUTABLE}" inspect)
run_and_expect(
    2
    "invalid resource limit argument"
    "${IROP_EXECUTABLE}"
    inspect
    "${IROP_FIXTURE}"
    --output-dir
    "${IROP_WORK_DIR}/invalid-limit-output"
    --max-triangles
    0
)
run_and_expect(
    3
    "missing input file"
    "${IROP_EXECUTABLE}"
    inspect
    "${IROP_WORK_DIR}/does-not-exist.stl"
    --output-dir
    "${IROP_WORK_DIR}/missing-input-output"
)
run_and_expect(
    4
    "resource limit"
    "${IROP_EXECUTABLE}"
    inspect
    "${IROP_FIXTURE}"
    --output-dir
    "${IROP_WORK_DIR}/resource-output"
    --max-input-bytes
    1
)

set(malformed_input "${IROP_WORK_DIR}/malformed.stl")
file(
    WRITE
    "${malformed_input}"
    "solid malformed\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\n"
)
run_and_expect(
    3
    "malformed input"
    "${IROP_EXECUTABLE}"
    inspect
    "${malformed_input}"
    --output-dir
    "${IROP_WORK_DIR}/malformed-output"
)

set(output_file "${IROP_WORK_DIR}/not-a-directory")
file(WRITE "${output_file}" "occupied")
run_and_expect(
    5
    "unusable output path"
    "${IROP_EXECUTABLE}"
    inspect
    "${IROP_FIXTURE}"
    --output-dir
    "${output_file}"
)

run_and_expect(
    0
    "valid inspection"
    "${IROP_EXECUTABLE}"
    inspect
    "${IROP_FIXTURE}"
    --output-dir
    "${valid_output}"
)

set(normalized_stl "${valid_output}/normalized.stl")
set(summary_path "${valid_output}/inspection-summary.json")
if(NOT EXISTS "${normalized_stl}" OR IS_DIRECTORY "${normalized_stl}")
    message(FATAL_ERROR "valid inspection did not produce normalized.stl")
endif()
if(NOT EXISTS "${summary_path}" OR IS_DIRECTORY "${summary_path}")
    message(FATAL_ERROR "valid inspection did not produce inspection-summary.json")
endif()
file(SIZE "${normalized_stl}" normalized_size)
if(normalized_size LESS 84)
    message(FATAL_ERROR "normalized STL is too small to contain a binary STL header")
endif()

file(READ "${summary_path}" summary_json)
string(JSON schema_version ERROR_VARIABLE schema_error GET "${summary_json}" schema_version)
if(schema_error OR NOT schema_version EQUAL 1)
    message(FATAL_ERROR "inspection summary has no supported schema_version: ${schema_error}")
endif()
string(JSON command ERROR_VARIABLE command_error GET "${summary_json}" command)
if(command_error OR NOT command STREQUAL "inspect")
    message(FATAL_ERROR "inspection summary has an invalid command: ${command_error}")
endif()
string(JSON outcome ERROR_VARIABLE outcome_error GET "${summary_json}" outcome category)
if(outcome_error OR NOT outcome STREQUAL "success")
    message(FATAL_ERROR "inspection summary has an invalid outcome: ${outcome_error}")
endif()
string(JSON vertex_count ERROR_VARIABLE vertex_error GET "${summary_json}" mesh vertex_count)
string(JSON triangle_count ERROR_VARIABLE triangle_error GET "${summary_json}" mesh triangle_count)
if(vertex_error OR triangle_error OR NOT vertex_count EQUAL 4 OR NOT triangle_count EQUAL 4)
    message(
        FATAL_ERROR
        "inspection summary has unexpected tetrahedron counts: vertices=${vertex_count}, triangles=${triangle_count}"
    )
endif()

run_and_expect(
    5
    "artifact collision"
    "${IROP_EXECUTABLE}"
    inspect
    "${IROP_FIXTURE}"
    --output-dir
    "${valid_output}"
)
