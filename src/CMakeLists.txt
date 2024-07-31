add_executable(ra-update
    ra-update.c
    tools.c
    uart.c
    ra_protocol.c
    ra_gpio.c
    fw_file.c
)

target_include_directories(ra-update
    PRIVATE
         ${LIBGGPIOD_INCLUDE_DIRS}
)

target_link_libraries(ra-update
    PRIVATE
        ${LIBGPIOD_LIBRARIES}
)

install(TARGETS ra-update DESTINATION sbin)

add_executable(ra-raw
    ra-raw.c
    tools.c
    uart.c
    cb_protocol.c
    crc8_j1850.c
)

install(TARGETS ra-raw DESTINATION sbin)
