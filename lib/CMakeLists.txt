set(LIBRAUTILS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PARENT_SCOPE)

add_library(ra-utils SHARED)

target_sources(ra-utils
    PRIVATE
        "cb_uart.c"
        "cb_protocol.c"
        "ra_protocol.c"
        "crc8_j1850.c"
        "logging.c"
        "tools.c"
        "uart.c"
)

target_link_libraries(ra-utils m)

set_target_properties(ra-utils
    PROPERTIES
       VERSION ${PROJECT_VERSION}
       SOVERSION 1
)

install(TARGETS ra-utils DESTINATION lib)
install(
    FILES
        "cb_uart.h"
        "cb_protocol.h"
        "ra_protocol.h"
        "logging.h"
        "uart.h"
    DESTINATION
        "include/ra-utils"
)
