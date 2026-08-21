include_guard(GLOBAL)

function(_irop_collect_visual_studio_llvm_hints output_variable)
    set(irop_hints)
    if(WIN32)
        find_program(
            irop_vswhere
            NAMES vswhere.exe
            HINTS "$ENV{SystemDrive}/Program Files (x86)/Microsoft Visual Studio/Installer"
            NO_CACHE
        )
        if(irop_vswhere)
            execute_process(
                COMMAND
                    "${irop_vswhere}"
                    -latest
                    -products
                    *
                    -requires
                    Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                    -property
                    installationPath
                RESULT_VARIABLE irop_vswhere_result
                OUTPUT_VARIABLE irop_visual_studio_root
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(irop_vswhere_result EQUAL 0 AND irop_visual_studio_root)
                file(TO_CMAKE_PATH "${irop_visual_studio_root}" irop_visual_studio_root)
                list(
                    APPEND irop_hints
                    "${irop_visual_studio_root}/VC/Tools/Llvm/x64/bin"
                    "${irop_visual_studio_root}/VC/Tools/Llvm/bin"
                )
            endif()
        endif()
    endif()
    set("${output_variable}" "${irop_hints}" PARENT_SCOPE)
endfunction()

function(irop_configure_clang_tidy)
    if(NOT IROP_ENABLE_CLANG_TIDY)
        return()
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        message(FATAL_ERROR "IROP_ENABLE_CLANG_TIDY requires a Ninja or Makefile generator")
    endif()

    _irop_collect_visual_studio_llvm_hints(irop_llvm_hints)
    find_program(
        IROP_CLANG_TIDY
        NAMES clang-tidy clang-tidy-22
        HINTS ${irop_llvm_hints}
        DOC "clang-tidy executable used for project analysis"
    )
    if(NOT IROP_CLANG_TIDY)
        message(FATAL_ERROR "clang-tidy was requested but not found; set IROP_CLANG_TIDY to its full path")
    endif()

    set(
        CMAKE_CXX_CLANG_TIDY
        "${IROP_CLANG_TIDY};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy;--extra-arg=/EHsc"
        PARENT_SCOPE
    )
endfunction()

function(irop_add_clang_format_targets)
    set(options)
    set(one_value_arguments)
    set(multi_value_arguments FILES)
    cmake_parse_arguments(PARSE_ARGV 0 IROP_FORMAT "${options}" "${one_value_arguments}" "${multi_value_arguments}")

    if(NOT IROP_FORMAT_FILES)
        return()
    endif()

    _irop_collect_visual_studio_llvm_hints(irop_llvm_hints)
    find_program(
        IROP_CLANG_FORMAT
        NAMES clang-format clang-format-22
        HINTS ${irop_llvm_hints}
        DOC "clang-format executable used for project formatting"
    )
    if(NOT IROP_CLANG_FORMAT)
        message(WARNING "clang-format was not found; irop-format and irop-format-check will be unavailable")
        return()
    endif()

    add_custom_target(
        irop-format-check
        COMMAND "${IROP_CLANG_FORMAT}" --style=file --dry-run --Werror ${IROP_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Checking project-owned C++ formatting"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
    add_custom_target(
        irop-format
        COMMAND "${IROP_CLANG_FORMAT}" --style=file -i ${IROP_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Formatting project-owned C++ files"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endfunction()
