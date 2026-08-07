if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "SOURCE_DIR and OUTPUT_DIR are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

function(repeat_file input output repeat_count)
    file(READ "${input}" content)
    file(WRITE "${output}" "")
    foreach(index RANGE 1 ${repeat_count})
        file(APPEND "${output}" "${content}")
    endforeach()
endfunction()

repeat_file(
    "${SOURCE_DIR}/LZRC_implementation_spec.md"
    "${OUTPUT_DIR}/medium-repeat.txt"
    40
)

repeat_file(
    "${SOURCE_DIR}/third_party/googletest/googletest/src/gtest.cc"
    "${OUTPUT_DIR}/far-repeat.txt"
    8
)

set(huge_block "")
foreach(relative_path IN ITEMS
    "third_party/googletest/googletest/src/gtest.cc"
    "third_party/googletest/googletest/test/gtest_unittest.cc"
    "third_party/googletest/googlemock/include/gmock/gmock-matchers.h"
    "third_party/googletest/docs/gmock_cook_book.md"
    "third_party/googletest/googlemock/test/gmock-matchers-containers_test.cc"
    "third_party/googletest/googletest/include/gtest/gtest.h"
)
    file(READ "${SOURCE_DIR}/${relative_path}" part)
    string(APPEND huge_block "\n/* LZRC corpus boundary */\n${part}")
endforeach()
file(WRITE "${OUTPUT_DIR}/huge-repeat.txt" "${huge_block}${huge_block}")
