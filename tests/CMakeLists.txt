add_executable(lzrc_tests
    bit_tree_test.cpp
    carc_test.cpp
    lzss_test.cpp
    profile_boundary_test.cpp
    lzrc_test.cpp
    math_test.cpp
    range_coder_test.cpp
)

target_link_libraries(lzrc_tests PRIVATE lzrc GTest::gtest_main)
target_compile_features(lzrc_tests PRIVATE cxx_std_17)
target_include_directories(lzrc_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)

if(MSVC)
    target_compile_options(lzrc_tests PRIVATE /W4 /WX)
else()
    target_compile_options(lzrc_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(GoogleTest)
gtest_discover_tests(lzrc_tests)

add_test(
    NAME CliTest.RoundTrip
    COMMAND ${CMAKE_COMMAND}
        -DLZRC_CLI=$<TARGET_FILE:lzrc_cli>
        -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli_test
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_roundtrip.cmake
)
