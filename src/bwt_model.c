#include "bwt.h"
#include "bit_tree.h"

#include <stdlib.h>

lzrc_result lzrc_bwt_encode_block(unsigned int stage, const uint8_t *last,
                                 size_t size, uint8_t *output,
                                 size_t capacity, size_t *written) {
    lzrc_range_encoder encoder;
    lzrc_context contexts[255];
    size_t i;
    (void)stage;
    lzrc_contexts_init(contexts, 255U);
    (void)lzrc_range_encoder_init(&encoder, 4U, output, capacity);
    for (i = 0U; i < size; ++i) {
        if (!lzrc_bit_tree_encode(&encoder, contexts, 8U, last[i]))
            return LZRC_ERROR_OUTPUT_TOO_SMALL;
    }
    if (!lzrc_range_encoder_finish(&encoder)) return LZRC_ERROR_OUTPUT_TOO_SMALL;
    *written = lzrc_range_encoder_size(&encoder);
    return LZRC_OK;
}

lzrc_result lzrc_bwt_decode_block(unsigned int stage, const uint8_t *input,
                                 size_t input_size, uint8_t *last, size_t size) {
    lzrc_range_decoder decoder;
    lzrc_context contexts[255];
    size_t i;
    (void)stage;
    lzrc_contexts_init(contexts, 255U);
    if (!lzrc_range_decoder_init(&decoder, 4U, input, input_size))
        return LZRC_ERROR_CORRUPT_INPUT;
    for (i = 0U; i < size; ++i) {
        uint32_t value;
        if (!lzrc_bit_tree_decode(&decoder, contexts, 8U, &value))
            return LZRC_ERROR_CORRUPT_INPUT;
        last[i] = (uint8_t)value;
    }
    if (decoder.state.range32.input_position != input_size)
        return LZRC_ERROR_CORRUPT_INPUT;
    return LZRC_OK;
}
