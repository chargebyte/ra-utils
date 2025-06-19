add_executable(ra-update
    ra-update.c
    ra_gpio.c
    fw_file.c
)

target_include_directories(ra-update
    PRIVATE
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBGPIOD_INCLUDE_DIRS}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(ra-update
    PRIVATE
        ra-utils
        ${LIBGPIOD_LIBRARIES}
)

install(TARGETS ra-update DESTINATION sbin)

add_executable(ra-raw
    ra-raw.c
)

target_include_directories(ra-raw
    PRIVATE
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(ra-raw
    PRIVATE
        ra-utils
)

install(TARGETS ra-raw DESTINATION sbin)

add_executable(ra-gen-param-block
    ra-gen-param-block.c
)

target_include_directories(ra-gen-param-block
    PRIVATE
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

install(TARGETS ra-gen-param-block DESTINATION bin)
