#include "carc.h"

#include "bit_tree.h"

#include <stddef.h>
#include <stdint.h>

#define LZRC_TREE4_CONTEXTS 15U
#define LZRC_TREE8_CONTEXTS 255U

static const uint32_t kOffsetBase[5] = {
    UINT32_C(1), UINT32_C(257), UINT32_C(4097), UINT32_C(65537),
    UINT32_C(1048577)};
static const uint32_t kOffsetMaximum[5] = {
    UINT32_C(256), UINT32_C(4096), UINT32_C(65536), UINT32_C(1048576),
    UINT32_C(16777216)};
static const unsigned int kOffsetDirectBits[5] = {0U, 4U, 8U, 12U, 16U};

lzrc_token lzrc_token_literal(uint8_t value) {
    lzrc_token token;
    token.type = LZRC_TOKEN_LITERAL;
    token.literal = value;
    token.length = 0U;
    token.offset = 0U;
    return token;
}

lzrc_token lzrc_token_match(uint16_t length, uint32_t offset) {
    lzrc_token token;
    token.type = LZRC_TOKEN_MATCH;
    token.literal = 0U;
    token.length = length;
    token.offset = offset;
    return token;
}

size_t lzrc_carc_context_count(unsigned int profile) {
    size_t count;
    if (profile > 4U) {
        return 0U;
    }
    count = 2U + LZRC_TREE8_CONTEXTS + LZRC_TREE4_CONTEXTS;
    if (profile >= 1U) {
        count += LZRC_TREE8_CONTEXTS;
    }
    if (profile == 4U) {
        count += LZRC_TREE8_CONTEXTS;
    }
    count += (size_t)(profile + 1U) * LZRC_TREE8_CONTEXTS;
    return count;
}

static lzrc_context *take_contexts(lzrc_context **cursor, size_t count) {
    lzrc_context *result = *cursor;
    *cursor += count;
    return result;
}

static bool carc_model_init(lzrc_carc_model *model,
                            unsigned int profile,
                            lzrc_context *storage,
                            size_t storage_count) {
    lzrc_context *cursor;
    unsigned int category;
    const size_t required = lzrc_carc_context_count(profile);
    if (required == 0U || storage == NULL || storage_count < required) {
        return false;
    }

    lzrc_contexts_init(storage, required);
    model->profile = profile;
    model->previous_was_match = false;
    model->length_more = NULL;
    model->length_extend = NULL;
    for (category = 0U; category < 5U; ++category) {
        model->offsets[category] = NULL;
    }

    cursor = storage;
    model->flags = take_contexts(&cursor, 2U);
    model->literal = take_contexts(&cursor, LZRC_TREE8_CONTEXTS);
    model->length_mini = take_contexts(&cursor, LZRC_TREE4_CONTEXTS);
    if (profile >= 1U) {
        model->length_more = take_contexts(&cursor, LZRC_TREE8_CONTEXTS);
    }
    if (profile == 4U) {
        model->length_extend = take_contexts(&cursor, LZRC_TREE8_CONTEXTS);
    }
    for (category = 0U; category <= profile; ++category) {
        model->offsets[category] =
            take_contexts(&cursor, LZRC_TREE8_CONTEXTS);
    }
    return true;
}

static uint16_t profile_maximum_length(unsigned int profile) {
    if (profile == 0U) {
        return 18U;
    }
    if (profile <= 3U) {
        return 274U;
    }
    return 530U;
}

static bool token_is_valid(unsigned int profile, const lzrc_token *token) {
    if (token->type == LZRC_TOKEN_LITERAL) {
        return true;
    }
    if (token->type != LZRC_TOKEN_MATCH) {
        return false;
    }
    return token->length >= 3U &&
           token->length <= profile_maximum_length(profile) &&
           token->offset >= 1U && token->offset <= kOffsetMaximum[profile];
}

static bool encode_direct_value(lzrc_range_encoder *range,
                                uint32_t value,
                                unsigned int bit_count) {
    unsigned int bit_position;
    for (bit_position = bit_count; bit_position > 0U; --bit_position) {
        const uint8_t bit =
            (uint8_t)((value >> (bit_position - 1U)) & UINT32_C(1));
        if (!lzrc_range_encoder_encode_direct_bit(range, bit)) {
            return false;
        }
    }
    return true;
}

static bool decode_direct_value(lzrc_range_decoder *range,
                                unsigned int bit_count,
                                uint32_t *value) {
    unsigned int index;
    uint32_t result = 0U;
    for (index = 0U; index < bit_count; ++index) {
        uint8_t bit;
        if (!lzrc_range_decoder_decode_direct_bit(range, &bit)) {
            return false;
        }
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}

static bool encode_unary(lzrc_range_encoder *range, unsigned int category) {
    unsigned int index;
    for (index = 0U; index < category; ++index) {
        if (!lzrc_range_encoder_encode_direct_bit(range, 1U)) {
            return false;
        }
    }
    return lzrc_range_encoder_encode_direct_bit(range, 0U);
}

static bool decode_unary(lzrc_range_decoder *range,
                         unsigned int maximum_category,
                         unsigned int *category) {
    unsigned int result = 0U;
    for (;;) {
        uint8_t bit;
        if (!lzrc_range_decoder_decode_direct_bit(range, &bit)) {
            return false;
        }
        if (bit == 0U) {
            *category = result;
            return true;
        }
        ++result;
        if (result > maximum_category) {
            return false;
        }
    }
}

bool lzrc_carc_encoder_init(lzrc_carc_encoder *encoder,
                            unsigned int profile,
                            uint8_t *output,
                            size_t output_capacity,
                            lzrc_context *context_storage,
                            size_t context_count) {
    if (!carc_model_init(&encoder->model, profile, context_storage,
                         context_count)) {
        return false;
    }
    return lzrc_range_encoder_init(&encoder->range, profile, output,
                                   output_capacity);
}

static bool encode_length(lzrc_carc_encoder *encoder, uint16_t length) {
    if (length <= 18U) {
        return encode_unary(&encoder->range, 0U) &&
               lzrc_bit_tree_encode(&encoder->range,
                                    encoder->model.length_mini, 4U,
                                    (uint32_t)(length - 3U));
    }
    if (length <= 274U) {
        return encode_unary(&encoder->range, 1U) &&
               lzrc_bit_tree_encode(&encoder->range,
                                    encoder->model.length_more, 8U,
                                    (uint32_t)(length - 19U));
    }
    return encode_unary(&encoder->range, 2U) &&
           lzrc_bit_tree_encode(&encoder->range,
                                encoder->model.length_extend, 8U,
                                (uint32_t)(length - 275U));
}

static bool encode_offset(lzrc_carc_encoder *encoder, uint32_t offset) {
    unsigned int category = 0U;
    uint32_t adjusted;
    uint32_t upper;
    uint32_t lower;
    const unsigned int profile = encoder->model.profile;
    while (category < profile && offset > kOffsetMaximum[category]) {
        ++category;
    }
    adjusted = offset - kOffsetBase[category];
    upper = adjusted >> kOffsetDirectBits[category];
    if (kOffsetDirectBits[category] == 0U) {
        lower = 0U;
    } else {
        lower = adjusted &
                ((UINT32_C(1) << kOffsetDirectBits[category]) - UINT32_C(1));
    }
    return encode_unary(&encoder->range, category) &&
           lzrc_bit_tree_encode(&encoder->range,
                                encoder->model.offsets[category], 8U, upper) &&
           encode_direct_value(&encoder->range, lower,
                               kOffsetDirectBits[category]);
}

bool lzrc_carc_encode_token(lzrc_carc_encoder *encoder,
                            const lzrc_token *token) {
    lzrc_context *flag_context;
    const uint8_t flag = (token->type == LZRC_TOKEN_MATCH) ? 1U : 0U;
    if (!token_is_valid(encoder->model.profile, token)) {
        return false;
    }
    flag_context = &encoder->model.flags[encoder->model.previous_was_match ? 1U
                                                                          : 0U];
    if (!lzrc_range_encoder_encode_bit(&encoder->range, flag_context, flag)) {
        return false;
    }
    if (token->type == LZRC_TOKEN_LITERAL) {
        if (!lzrc_bit_tree_encode(&encoder->range, encoder->model.literal, 8U,
                                  token->literal)) {
            return false;
        }
        encoder->model.previous_was_match = false;
        return true;
    }
    if (!encode_length(encoder, token->length) ||
        !encode_offset(encoder, token->offset)) {
        return false;
    }
    encoder->model.previous_was_match = true;
    return true;
}

bool lzrc_carc_encoder_finish(lzrc_carc_encoder *encoder) {
    return lzrc_range_encoder_finish(&encoder->range);
}

size_t lzrc_carc_encoder_size(const lzrc_carc_encoder *encoder) {
    return lzrc_range_encoder_size(&encoder->range);
}

bool lzrc_carc_decoder_init(lzrc_carc_decoder *decoder,
                            unsigned int profile,
                            const uint8_t *input,
                            size_t input_size,
                            lzrc_context *context_storage,
                            size_t context_count) {
    if (!carc_model_init(&decoder->model, profile, context_storage,
                         context_count)) {
        return false;
    }
    return lzrc_range_decoder_init(&decoder->range, profile, input, input_size);
}

static bool decode_length(lzrc_carc_decoder *decoder, uint16_t *length) {
    unsigned int category;
    uint32_t value;
    const unsigned int maximum_category =
        (decoder->model.profile == 0U) ? 0U
                                      : ((decoder->model.profile == 4U) ? 2U : 1U);
    if (!decode_unary(&decoder->range, maximum_category, &category)) {
        return false;
    }
    if (category == 0U) {
        if (!lzrc_bit_tree_decode(&decoder->range,
                                  decoder->model.length_mini, 4U, &value)) {
            return false;
        }
        *length = (uint16_t)(value + 3U);
        return true;
    }
    if (category == 1U) {
        if (!lzrc_bit_tree_decode(&decoder->range,
                                  decoder->model.length_more, 8U, &value)) {
            return false;
        }
        *length = (uint16_t)(value + 19U);
        return true;
    }
    if (!lzrc_bit_tree_decode(&decoder->range,
                              decoder->model.length_extend, 8U, &value)) {
        return false;
    }
    *length = (uint16_t)(value + 275U);
    return true;
}

static bool decode_offset(lzrc_carc_decoder *decoder, uint32_t *offset) {
    unsigned int category;
    uint32_t upper;
    uint32_t lower;
    uint32_t adjusted;
    if (!decode_unary(&decoder->range, decoder->model.profile, &category) ||
        !lzrc_bit_tree_decode(&decoder->range,
                              decoder->model.offsets[category], 8U, &upper) ||
        !decode_direct_value(&decoder->range, kOffsetDirectBits[category],
                             &lower)) {
        return false;
    }
    adjusted = (upper << kOffsetDirectBits[category]) | lower;
    *offset = adjusted + kOffsetBase[category];
    return *offset <= kOffsetMaximum[category];
}

bool lzrc_carc_decode_token(lzrc_carc_decoder *decoder, lzrc_token *token) {
    uint8_t flag;
    lzrc_context *flag_context =
        &decoder->model.flags[decoder->model.previous_was_match ? 1U : 0U];
    if (!lzrc_range_decoder_decode_bit(&decoder->range, flag_context, &flag)) {
        return false;
    }
    if (flag == 0U) {
        uint32_t literal;
        if (!lzrc_bit_tree_decode(&decoder->range, decoder->model.literal, 8U,
                                  &literal)) {
            return false;
        }
        *token = lzrc_token_literal((uint8_t)literal);
        decoder->model.previous_was_match = false;
        return true;
    }
    token->type = LZRC_TOKEN_MATCH;
    token->literal = 0U;
    if (!decode_length(decoder, &token->length) ||
        !decode_offset(decoder, &token->offset)) {
        return false;
    }
    if (!token_is_valid(decoder->model.profile, token)) {
        return false;
    }
    decoder->model.previous_was_match = true;
    return true;
}
