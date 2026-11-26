#include "bwt.h"
#include "bit_tree.h"

#include <stdlib.h>

lzrc_result lzrc_bwt_encode_block(unsigned int stage, const uint8_t *last,
                                 size_t size, uint8_t *output,
                                 size_t capacity, size_t *written) {
    lzrc_range_encoder encoder;
    const size_t count = stage == 2U ? 256U * 255U : 255U;
    lzrc_context *contexts = (lzrc_context *)malloc(count * sizeof(lzrc_context));
    uint8_t previous = 0U;
    size_t i;
    if (contexts == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    lzrc_contexts_init(contexts, count);
    (void)lzrc_range_encoder_init(&encoder, 4U, output, capacity);
    for (i = 0U; i < size; ++i) {
        lzrc_context *tree = contexts + (stage == 2U ? previous * 255U : 0U);
        if (!lzrc_bit_tree_encode(&encoder, tree, 8U, last[i])) {
            free(contexts);
            return LZRC_ERROR_OUTPUT_TOO_SMALL;
        }
        previous = last[i];
    }
    free(contexts);
    if (!lzrc_range_encoder_finish(&encoder)) return LZRC_ERROR_OUTPUT_TOO_SMALL;
    *written = lzrc_range_encoder_size(&encoder);
    return LZRC_OK;
}

lzrc_result lzrc_bwt_decode_block(unsigned int stage, const uint8_t *input,
                                 size_t input_size, uint8_t *last, size_t size) {
    lzrc_range_decoder decoder;
    const size_t count = stage == 2U ? 256U * 255U : 255U;
    lzrc_context *contexts;
    uint8_t previous = 0U;
    size_t i;
    if (!lzrc_range_decoder_init(&decoder, 4U, input, input_size))
        return LZRC_ERROR_CORRUPT_INPUT;
    contexts = (lzrc_context *)malloc(count * sizeof(lzrc_context));
    if (contexts == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    lzrc_contexts_init(contexts, count);
    for (i = 0U; i < size; ++i) {
        uint32_t value;
        lzrc_context *tree = contexts + (stage == 2U ? previous * 255U : 0U);
        if (!lzrc_bit_tree_decode(&decoder, tree, 8U, &value)) {
            free(contexts);
            return LZRC_ERROR_CORRUPT_INPUT;
        }
        last[i] = (uint8_t)value;
        previous = last[i];
    }
    free(contexts);
    if (decoder.state.range32.input_position != input_size)
        return LZRC_ERROR_CORRUPT_INPUT;
    return LZRC_OK;
}
