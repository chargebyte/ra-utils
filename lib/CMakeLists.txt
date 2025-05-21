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

target_link_libraries(ra-utils m)

set_target_properties(ra-utils
    PROPERTIES
       VERSION ${PROJECT_VERSION}
       SOVERSION 1
)

set(INCLUDE_INSTALL_DIR ${CMAKE_INSTALL_INCLUDEDIR})

install(
    TARGETS ra-utils
    EXPORT ra-utilsTargets
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    EXPORT ra-utilsTargets
    FILE ra-utilsTargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ra-utils
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ra-utilsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ra-utilsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ra-utils
    PATH_VARS INCLUDE_INSTALL_DIR
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/ra-utilsConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ra-utils
)

# pkg-config file generation
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ra-utils.pc.in ra-utils.pc @ONLY)
install(
    FILES ${CMAKE_BINARY_DIR}/lib/ra-utils.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
