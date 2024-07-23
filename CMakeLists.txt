cmake_minimum_required(VERSION 3.10)

project(renesas_updater
    VERSION 0.1.0
    DESCRIPTION "Command line tool to flash the Renesas MCU (aka safety controller)"
    LANGUAGES C
)

# search for package PkgConfig
find_package(PkgConfig REQUIRED)

# search for libgpiod
pkg_search_module(LIBGPIOD REQUIRED libgpiod)

add_subdirectory(src)

include(GNUInstallDirs)

install(
    FILES safety_sw.bin
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}
)
