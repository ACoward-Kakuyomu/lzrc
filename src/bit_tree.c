#include "bit_tree.h"

#include <stddef.h>
#include <stdint.h>

#define LZRC_BIT_TREE_MAX_WIDTH 16U

bool lzrc_bit_tree_encode(lzrc_range_encoder *encoder,
                          lzrc_context *contexts,
                          unsigned int bit_width,
                          uint32_t value) {
    size_t context_index = 1U;
    unsigned int bit_position;
    uint32_t value_count;

    if (bit_width == 0U || bit_width > LZRC_BIT_TREE_MAX_WIDTH) {
        return false;
    }
    value_count = UINT32_C(1) << bit_width;
    if (value >= value_count) {
        return false;
    }

    for (bit_position = bit_width; bit_position > 0U; --bit_position) {
        const uint8_t bit =
            (uint8_t)((value >> (bit_position - 1U)) & UINT32_C(1));
        if (!lzrc_range_encoder_encode_bit(encoder, &contexts[context_index - 1U],
                                           bit)) {
            return false;
        }
        context_index = (context_index << 1U) | bit;
    }
    return true;
}

bool lzrc_bit_tree_decode(lzrc_range_decoder *decoder,
                          lzrc_context *contexts,
                          unsigned int bit_width,
                          uint32_t *value) {
    size_t context_index = 1U;
    unsigned int index;

    if (bit_width == 0U || bit_width > LZRC_BIT_TREE_MAX_WIDTH) {
        return false;
    }
    for (index = 0U; index < bit_width; ++index) {
        uint8_t bit;
        if (!lzrc_range_decoder_decode_bit(decoder, &contexts[context_index - 1U],
                                           &bit)) {
            return false;
        }
        context_index = (context_index << 1U) | bit;
    }
    *value = (uint32_t)(context_index - ((size_t)1U << bit_width));
    return true;
}
