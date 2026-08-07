cmake_minimum_required(VERSION 3.25)

project(lzrc VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

add_library(lzrc
    src/lzrc.c
    src/lzrc_math.c
    src/range_coder.c
    src/bit_tree.c
    src/carc.c
    src/lzss.c
)

target_include_directories(lzrc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(lzrc PUBLIC c_std_99)

if(MSVC)
    target_compile_options(lzrc PRIVATE /W4 /WX)
else()
    target_compile_options(lzrc PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(lzrc_cli src/lzrc_cli.c)
target_link_libraries(lzrc_cli PRIVATE lzrc)
target_compile_features(lzrc_cli PRIVATE c_std_99)
set_target_properties(lzrc_cli PROPERTIES OUTPUT_NAME lzrc)

if(MSVC)
    target_compile_options(lzrc_cli PRIVATE /W4 /WX)
    target_compile_definitions(lzrc_cli PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(lzrc_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS lzrc lzrc_cli
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
    add_subdirectory(tests)
endif()
