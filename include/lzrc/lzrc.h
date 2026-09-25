#ifndef LZRC_H
#define LZRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum lzrc_result {
    LZRC_OK = 0,
    LZRC_ERROR_INVALID_ARGUMENT,
    LZRC_ERROR_INVALID_PROFILE,
    LZRC_ERROR_OUTPUT_TOO_SMALL,
    LZRC_ERROR_CORRUPT_INPUT,
    LZRC_ERROR_OUT_OF_MEMORY
} lzrc_result;

typedef enum lzrc_compression_mode {
    LZRC_COMPRESSION_MODE_ORIGINAL = 0,
    LZRC_COMPRESSION_MODE_FAST = 1
} lzrc_compression_mode;

lzrc_result lzrc_compress_bound(size_t input_size, size_t *bound);

lzrc_result lzrc_compress(unsigned int profile,
                          const uint8_t *input,
                          size_t input_size,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_size);

lzrc_result lzrc_compress_with_mode(unsigned int profile,
                                    lzrc_compression_mode mode,
                                    const uint8_t *input,
                                    size_t input_size,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_size);

lzrc_result lzrc_decompress(unsigned int profile,
                            const uint8_t *input,
                            size_t input_size,
                            size_t expected_output_size,
                            uint8_t *output,
                            size_t output_capacity,
                            size_t *output_size);

const char *lzrc_result_string(lzrc_result result);

#ifdef __cplusplus
}
#endif

#endif
