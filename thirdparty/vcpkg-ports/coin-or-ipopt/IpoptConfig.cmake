get_filename_component(_IPOPT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT TARGET Ipopt::Ipopt)
    add_library(Ipopt::Ipopt SHARED IMPORTED)
    set_target_properties(
        Ipopt::Ipopt
        PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_IMPLIB_DEBUG "${_IPOPT_PREFIX}/debug/lib/ipopt.dll.lib"
            IMPORTED_IMPLIB_RELEASE "${_IPOPT_PREFIX}/lib/ipopt.dll.lib"
            IMPORTED_LOCATION_DEBUG "${_IPOPT_PREFIX}/debug/bin/ipopt-3.dll"
            IMPORTED_LOCATION_RELEASE "${_IPOPT_PREFIX}/bin/ipopt-3.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${_IPOPT_PREFIX}/include/coin-or"
            MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    )
endif()

set(Ipopt_VERSION 3.14.19)
set(Ipopt_FOUND TRUE)

unset(_IPOPT_PREFIX)
