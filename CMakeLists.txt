cmake_minimum_required(VERSION 3.10)

project(ra-utils
    VERSION 0.2.0
    DESCRIPTION "Command line tools related to the Renesas MCU (aka safety controller)"
    LANGUAGES C
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# latest firmware and example parameter blocks
add_subdirectory(firmware)

# search for package PkgConfig
find_package(PkgConfig REQUIRED)

# search for libgpiod
pkg_search_module(LIBGPIOD REQUIRED libgpiod)

add_subdirectory(lib)
add_subdirectory(src)
