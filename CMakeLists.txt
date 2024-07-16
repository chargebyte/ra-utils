cmake_minimum_required(VERSION 3.10)

project(Renesas_Updater)

# Find the libgpiod package
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod)

add_executable(renesas_updater main.c)


# Include the directories for libgpiod
include_directories(${GPIOD_INCLUDE_DIRS})

# Link the libgpiod library
target_link_libraries(renesas_updater ${GPIOD_LIBRARIES})

