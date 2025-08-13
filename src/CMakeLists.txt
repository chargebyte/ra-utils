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
    param_block.c
    param_block_crc8.c
)

target_include_directories(ra-gen-param-block
    PRIVATE
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(ra-gen-param-block
    PRIVATE
        m
)

install(TARGETS ra-gen-param-block DESTINATION bin)

add_executable(ra-pb-dump
    ra-pb-dump.c
    param_block.c
    param_block_crc8.c
)

target_include_directories(ra-pb-dump
    PRIVATE
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(ra-pb-dump
    PRIVATE
        m
)

install(TARGETS ra-pb-dump DESTINATION bin)

add_executable(ra-pb-create
    ra-pb-create.c
    param_block.c
    param_block_crc8.c
)

target_include_directories(ra-pb-create
    PRIVATE
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
        ${LIBYAML_INCLUDE_DIRS}
)

target_link_libraries(ra-pb-create
    PRIVATE
        m
        ${LIBYAML_LIBRARIES}
)

install(TARGETS ra-pb-create DESTINATION bin)
