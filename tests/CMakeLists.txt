find_package(GTest REQUIRED)
include(GoogleTest)

file(GLOB RA_PB_DUMP_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/*.bin")

add_executable(test-param-block
    test-param-block.cpp
    ${CMAKE_SOURCE_DIR}/src/param_block.c
    ${CMAKE_SOURCE_DIR}/src/param_block_crc8.c
)

target_include_directories(test-param-block
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(test-param-block
    PRIVATE
        GTest::gtest_main
        m
)

target_compile_features(test-param-block PRIVATE cxx_std_17)

gtest_discover_tests(test-param-block)

add_executable(test-tools
    test-tools.cpp
    ${CMAKE_SOURCE_DIR}/lib/tools.c
)

target_include_directories(test-tools
    PRIVATE
        ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(test-tools
    PRIVATE
        GTest::gtest_main
)

target_compile_features(test-tools PRIVATE cxx_std_17)

gtest_discover_tests(test-tools NO_PRETTY_VALUES)

add_executable(test-cb-uart
    test-cb-uart.cpp
)

target_include_directories(test-cb-uart
    PRIVATE
        ${CMAKE_SOURCE_DIR}/lib
)

target_link_libraries(test-cb-uart
    PRIVATE
        GTest::gtest_main
        ra-utils
)

target_compile_features(test-cb-uart PRIVATE cxx_std_17)

gtest_discover_tests(test-cb-uart)

add_executable(test-ra-pb-dump
    test-ra-pb-dump.cpp
    ${CMAKE_SOURCE_DIR}/src/param_block.c
    ${CMAKE_SOURCE_DIR}/src/param_block_crc8.c
)

target_include_directories(test-ra-pb-dump
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(test-ra-pb-dump
    PRIVATE
        GTest::gtest_main
        m
)

target_compile_features(test-ra-pb-dump PRIVATE cxx_std_17)

target_compile_definitions(test-ra-pb-dump
    PRIVATE
        RA_PB_DUMP_PATH="$<TARGET_FILE:ra-pb-dump>"
        RA_PB_DUMP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_dependencies(test-ra-pb-dump ra-pb-dump)

gtest_discover_tests(test-ra-pb-dump)

add_executable(test-ra-pb-create
    test-ra-pb-create.cpp
    ${CMAKE_SOURCE_DIR}/src/param_block.c
    ${CMAKE_SOURCE_DIR}/src/param_block_crc8.c
)

target_include_directories(test-ra-pb-create
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${LIBRAUTILS_INCLUDE_DIR}
        ${LIBRAUTILS_INCLUDE_BINARY_DIR}
)

target_link_libraries(test-ra-pb-create
    PRIVATE
        GTest::gtest_main
        m
)

target_compile_features(test-ra-pb-create PRIVATE cxx_std_17)

target_compile_definitions(test-ra-pb-create
    PRIVATE
        RA_PB_CREATE_PATH="$<TARGET_FILE:ra-pb-create>"
        RA_PB_DUMP_PATH="$<TARGET_FILE:ra-pb-dump>"
        RA_PB_CREATE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_dependencies(test-ra-pb-create ra-pb-create ra-pb-dump)

gtest_discover_tests(test-ra-pb-create)

add_executable(test-eol-decoder test-eol-decoder.cpp)
target_include_directories(test-eol-decoder PRIVATE ${LIBRAUTILS_INCLUDE_DIR})
target_link_libraries(test-eol-decoder PRIVATE GTest::gtest_main ra-utils)
target_compile_features(test-eol-decoder PRIVATE cxx_std_17)
gtest_discover_tests(test-eol-decoder)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ra-eol-integration
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test-ra-eol.py
            $<TARGET_FILE:ra-eol> $<TARGET_FILE:ra-candump> $<TARGET_FILE:ra-eol-test-gpio>)
set_tests_properties(ra-eol-integration PROPERTIES TIMEOUT 45)

add_executable(test-eol-state test-eol-state.cpp ${CMAKE_SOURCE_DIR}/src/eol_state.c)
target_include_directories(test-eol-state PRIVATE ${CMAKE_SOURCE_DIR}/src ${LIBRAUTILS_INCLUDE_DIR})
target_link_libraries(test-eol-state PRIVATE GTest::gtest_main ra-utils)
target_compile_features(test-eol-state PRIVATE cxx_std_17)
gtest_discover_tests(test-eol-state)

add_executable(ra-eol-test-gpio
    ${CMAKE_SOURCE_DIR}/src/ra-eol.c
    ${CMAKE_SOURCE_DIR}/src/eol_state.c
    fake-eol-gpio.c)
target_include_directories(ra-eol-test-gpio PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${LIBRAUTILS_INCLUDE_DIR} ${LIBRAUTILS_INCLUDE_BINARY_DIR} ${LIBGPIOD_INCLUDE_DIRS})
target_link_libraries(ra-eol-test-gpio PRIVATE ra-utils)
