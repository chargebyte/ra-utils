cmake_minimum_required(VERSION 3.10)

project(ra-utils
    VERSION 0.2.0
    DESCRIPTION "Command line tools related to the Renesas MCU (aka safety controller)"
    LANGUAGES C
)

# search for package PkgConfig
find_package(PkgConfig REQUIRED)

# search for libgpiod
pkg_search_module(LIBGPIOD REQUIRED libgpiod)

add_subdirectory(libcbuart)
add_subdirectory(src)

include(GNUInstallDirs)

install(
    FILES chargeSOM_fw_v_00_02_00.bin chargeSOM_parameter-block_only-contactor.bin
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}
)
