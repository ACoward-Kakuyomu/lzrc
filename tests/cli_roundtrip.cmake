if(NOT DEFINED LZRC_CLI OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "LZRC_CLI and TEST_DIR are required")
endif()

file(MAKE_DIRECTORY "${TEST_DIR}")
set(input "${TEST_DIR}/input.bin")
set(compressed "${TEST_DIR}/compressed.lzrc")
set(decoded "${TEST_DIR}/decoded.bin")
file(WRITE "${input}" "LZRC CLI round trip: abcabcabcabc\n")

foreach(profile RANGE 0 4)
    execute_process(
        COMMAND "${LZRC_CLI}" "c${profile}" "${input}" "${compressed}"
        RESULT_VARIABLE compress_result
    )
    if(NOT compress_result EQUAL 0)
        message(FATAL_ERROR "compression failed for profile ${profile}")
    endif()
    execute_process(
        COMMAND "${LZRC_CLI}" d "${compressed}" "${decoded}"
        RESULT_VARIABLE decompress_result
    )
    if(NOT decompress_result EQUAL 0)
        message(FATAL_ERROR "decompression failed for profile ${profile}")
    endif()
    file(SHA256 "${input}" input_hash)
    file(SHA256 "${decoded}" decoded_hash)
    if(NOT input_hash STREQUAL decoded_hash)
        message(FATAL_ERROR "round trip mismatch for profile ${profile}")
    endif()
endforeach()
