vcpkg_check_linkage(ONLY_DYNAMIC_LIBRARY ONLY_DYNAMIC_CRT)

set(VCPKG_POLICY_DLLS_WITHOUT_LIBS enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_POLICY_SKIP_MISPLACED_CMAKE_FILES_CHECK enabled)

if(NOT VCPKG_TARGET_IS_WINDOWS OR NOT VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    message(FATAL_ERROR "The project Ipopt overlay supports only Windows x64")
endif()

vcpkg_download_distfile(
    RELEASE_ARCHIVE
    URLS
        "https://github.com/coin-or/Ipopt/releases/download/releases/3.14.19/Ipopt-3.14.19-win64-msvs2022-md.zip"
    FILENAME "Ipopt-3.14.19-win64-msvs2022-md.zip"
    SHA512
        15312d94293ccc2e89f11d2355d619116a550ed65a083c9299cc60db379ef6da85984a6fedfd8336f442fb30dff5f507dbc5d4b25d324768d2e18b439eee0e54
)
vcpkg_extract_source_archive(RELEASE_ROOT ARCHIVE "${RELEASE_ARCHIVE}")

set(
    RELEASE_DLLS
    coinmumps-3.dll
    ipopt-3.dll
    libifcoremd.dll
    libiomp5md.dll
    libmmd.dll
    svml_dispmd.dll
)
foreach(DLL_NAME IN LISTS RELEASE_DLLS)
    file(INSTALL "${RELEASE_ROOT}/bin/${DLL_NAME}" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
endforeach()

file(INSTALL "${RELEASE_ROOT}/lib/ipopt.dll.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

if(NOT VCPKG_BUILD_TYPE)
    vcpkg_download_distfile(
        DEBUG_ARCHIVE
        URLS
            "https://github.com/coin-or/Ipopt/releases/download/releases/3.14.19/Ipopt-3.14.19-win64-msvs2022-mdd.zip"
        FILENAME "Ipopt-3.14.19-win64-msvs2022-mdd.zip"
        SHA512
            441c6e13ac95da577f94f975e986c958979c15b7134a9530778baae72b2311967b1200d35967931a2883befe81201f6482a98aef961ab2ff2a6ed80d0a42f1d6
    )
    vcpkg_extract_source_archive(DEBUG_ROOT ARCHIVE "${DEBUG_ARCHIVE}")
    set(
        DEBUG_DLLS
        coinmumps-3.dll
        ipopt-3.dll
        libifcoremdd.dll
        libiomp5md.dll
        libmmd.dll
        libmmdd.dll
        svml_dispmd.dll
    )
    foreach(DLL_NAME IN LISTS DEBUG_DLLS)
        file(INSTALL "${DEBUG_ROOT}/bin/${DLL_NAME}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin")
    endforeach()
    file(INSTALL "${DEBUG_ROOT}/lib/ipopt.dll.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    file(
        INSTALL
            "${DEBUG_ROOT}/bin/coinmumps-3.pdb"
            "${DEBUG_ROOT}/bin/ipopt-3.pdb"
        DESTINATION "${CURRENT_PACKAGES_DIR}/debug/bin"
    )
endif()
file(COPY "${RELEASE_ROOT}/include" DESTINATION "${CURRENT_PACKAGES_DIR}")

file(
    INSTALL
        "${CMAKE_CURRENT_LIST_DIR}/IpoptConfig.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/IpoptConfigVersion.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/Ipopt"
)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
file(
    INSTALL "${RELEASE_ROOT}/README.txt"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME upstream-binary-build.txt
)
file(
    COPY "${RELEASE_ROOT}/share/doc/ipopt/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}/upstream-doc"
)
vcpkg_install_copyright(FILE_LIST "${RELEASE_ROOT}/share/doc/ipopt/LICENSE")
