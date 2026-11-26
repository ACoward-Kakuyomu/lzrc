#include "bwt.h"

#include <stdlib.h>
#include <string.h>

#define BWT_HEADER_SIZE 12U
#define BWT_BLOCK_HEADER_SIZE 8U

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U); p[3] = (uint8_t)(value >> 24U);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

lzrc_result lzrc_bwt_compress_bound(size_t input_size, unsigned int blocks,
                                   size_t *bound) {
    size_t count, overhead;
    if (bound == NULL || blocks < 1U || blocks > 8U || input_size > UINT32_MAX)
        return LZRC_ERROR_INVALID_ARGUMENT;
    count = input_size / (blocks * LZRC_BWT_BLOCK_UNIT) +
            (input_size % (blocks * LZRC_BWT_BLOCK_UNIT) != 0U ? 1U : 0U);
    overhead = BWT_HEADER_SIZE + count * (BWT_BLOCK_HEADER_SIZE + 4U);
    /* At most two normalization bytes per coded bit, plus block flushes. */
    if (input_size > (SIZE_MAX - overhead) / 32U)
        return LZRC_ERROR_INVALID_ARGUMENT;
    *bound = overhead + input_size * 32U;
    return LZRC_OK;
}

lzrc_result lzrc_bwt_compress(unsigned int blocks,
                             const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_size) {
    size_t ignored, position = BWT_HEADER_SIZE, offset = 0U;
    size_t block_size = blocks * (size_t)LZRC_BWT_BLOCK_UNIT;
    uint8_t *last;
    lzrc_result result;
    if (output_size == NULL) return LZRC_ERROR_INVALID_ARGUMENT;
    *output_size = 0U;
    result = lzrc_bwt_compress_bound(input_size, blocks, &ignored);
    if (result != LZRC_OK || (input_size != 0U && input == NULL) || output == NULL)
        return LZRC_ERROR_INVALID_ARGUMENT;
    if (output_capacity < BWT_HEADER_SIZE) return LZRC_ERROR_OUTPUT_TOO_SMALL;
    memcpy(output, "LBWT", 4U);
    output[4] = LZRC_BWT_STAGE; output[5] = (uint8_t)blocks;
    output[6] = 0U; output[7] = 0U;
    put32(output + 8U, (uint32_t)input_size);
    if (input_size == 0U) { *output_size = position; return LZRC_OK; }
    last = (uint8_t *)malloc(input_size < block_size ? input_size : block_size);
    if (last == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    while (offset < input_size) {
        uint32_t primary = 0U;
        size_t encoded = 0U, n = input_size - offset;
        if (n > block_size) n = block_size;
        if (output_capacity - position < BWT_BLOCK_HEADER_SIZE) {
            result = LZRC_ERROR_OUTPUT_TOO_SMALL; break;
        }
        result = lzrc_bwt_forward(input + offset, n, last, &primary);
        if (result != LZRC_OK) break;
        result = lzrc_bwt_encode_block(LZRC_BWT_STAGE, last, n,
                    output + position + BWT_BLOCK_HEADER_SIZE,
                    output_capacity - position - BWT_BLOCK_HEADER_SIZE, &encoded);
        if (result != LZRC_OK) break;
        put32(output + position, primary);
        put32(output + position + 4U, (uint32_t)encoded);
        position += BWT_BLOCK_HEADER_SIZE + encoded;
        offset += n;
    }
    free(last);
    if (result == LZRC_OK) *output_size = position;
    return result;
}

lzrc_result lzrc_bwt_decompress(const uint8_t *input, size_t input_size,
                               size_t expected_output_size,
                               uint8_t *output, size_t output_capacity,
                               size_t *output_size) {
    size_t block_size, offset = 0U, position = BWT_HEADER_SIZE;
    unsigned int stage;
    uint8_t *last;
    lzrc_result result = LZRC_OK;
    if (output_size == NULL) return LZRC_ERROR_INVALID_ARGUMENT;
    *output_size = 0U;
    if (input == NULL || (expected_output_size != 0U && output == NULL))
        return LZRC_ERROR_INVALID_ARGUMENT;
    if (input_size < BWT_HEADER_SIZE || memcmp(input, "LBWT", 4U) != 0 ||
        input[4] < 1U || input[4] > LZRC_BWT_STAGE ||
        input[5] < 1U || input[5] > 8U || input[6] != 0U || input[7] != 0U ||
        get32(input + 8U) != expected_output_size)
        return LZRC_ERROR_CORRUPT_INPUT;
    if (output_capacity < expected_output_size) return LZRC_ERROR_OUTPUT_TOO_SMALL;
    stage = input[4];
    block_size = input[5] * (size_t)LZRC_BWT_BLOCK_UNIT;
    if (expected_output_size == 0U)
        return input_size == position ? LZRC_OK : LZRC_ERROR_CORRUPT_INPUT;
    last = (uint8_t *)malloc(expected_output_size < block_size ?
                              expected_output_size : block_size);
    if (last == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    while (offset < expected_output_size) {
        uint32_t primary, encoded;
        size_t n = expected_output_size - offset;
        if (n > block_size) n = block_size;
        if (input_size - position < BWT_BLOCK_HEADER_SIZE) {
            result = LZRC_ERROR_CORRUPT_INPUT; break;
        }
        primary = get32(input + position);
        encoded = get32(input + position + 4U);
        position += BWT_BLOCK_HEADER_SIZE;
        if (primary >= n || encoded > input_size - position) {
            result = LZRC_ERROR_CORRUPT_INPUT; break;
        }
        result = lzrc_bwt_decode_block(stage, input + position, encoded, last, n);
        if (result != LZRC_OK) break;
        result = lzrc_bwt_inverse(last, n, primary, output + offset);
        if (result != LZRC_OK) break;
        offset += n;
        position += encoded;
    }
    free(last);
    if (result == LZRC_OK && position != input_size)
        result = LZRC_ERROR_CORRUPT_INPUT;
    if (result == LZRC_OK) *output_size = offset;
    return result;
}
