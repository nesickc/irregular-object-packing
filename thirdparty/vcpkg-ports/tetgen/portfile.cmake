vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO TetGen/TetGen
    REF 535f9c41f44abc832a7bbf2c9c7af003d1c18f3c
    SHA512 62e5fc640f72e594ad7d7286075f85cb590d4a71b979e0b035d545543e4d80807db26c2f56775032e4a94abbdaf411473273bd304ad77bf1a451c9db435dcfcf
    HEAD_REF main
)

# The v1.6.0 source has no install or package-export rules. Build it through a
# port-owned wrapper so the pinned upstream source remains unmodified.
vcpkg_cmake_configure(
    SOURCE_PATH "${CURRENT_PORT_DIR}"
    OPTIONS "-DTETGEN_SOURCE_DIR=${SOURCE_PATH}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(
    PACKAGE_NAME tetgen
    CONFIG_PATH share/tetgen
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
