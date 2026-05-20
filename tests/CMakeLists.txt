find_package(GTest REQUIRED)
include(GoogleTest)

file(GLOB RA_PB_DUMP_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/*.bin")

add_executable(test-ra-pb-dump
    test-ra-pb-dump.cpp
)

target_link_libraries(test-ra-pb-dump
    PRIVATE
        GTest::gtest_main
)

target_compile_features(test-ra-pb-dump PRIVATE cxx_std_17)

target_compile_definitions(test-ra-pb-dump
    PRIVATE
        RA_PB_DUMP_PATH="$<TARGET_FILE:ra-pb-dump>"
        RA_PB_DUMP_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_dependencies(test-ra-pb-dump ra-pb-dump)

gtest_discover_tests(test-ra-pb-dump)
