add_executable(ra-update
    ra-update.c
    ra_gpio.c
    fw_file.c
)

target_include_directories(ra-update
    PRIVATE
        ${LIBCBUART_INCLUDE_DIR}
        ${LIBGPIOD_INCLUDE_DIRS}
)

target_link_libraries(ra-update
    PRIVATE
        libcbuart
        ${LIBGPIOD_LIBRARIES}
)

install(TARGETS ra-update DESTINATION sbin)

add_executable(ra-raw
    ra-raw.c
)

target_include_directories(ra-raw
    PRIVATE
        ${LIBCBUART_INCLUDE_DIR}
)

target_link_libraries(ra-raw
    PRIVATE
        libcbuart
)

install(TARGETS ra-raw DESTINATION sbin)
