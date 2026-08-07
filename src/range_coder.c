#include "range_coder.h"

#include "lzrc/lzrc_math.h"

#include <stddef.h>
#include <stdint.h>

#define LZRC_INITIAL_PROBABILITY UINT8_C(128)
#define LZRC_RANGE16_TOP UINT16_C(0x1000)
#define LZRC_RANGE16_BOT UINT16_C(0x0100)
#define LZRC_RANGE32_TOP UINT32_C(0x01000000)
#define LZRC_RANGE32_BOT UINT32_C(0x00010000)

void lzrc_contexts_init(lzrc_context *contexts, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        contexts[index].probability = LZRC_INITIAL_PROBABILITY;
    }
}

uint8_t lzrc_probability_update(uint8_t probability, uint8_t bit) {
    if (bit == 0U) {
        return (uint8_t)(probability +
                         (uint8_t)((UINT8_C(255) - probability) >> 5U));
    }
    return (uint8_t)(probability - (uint8_t)(probability >> 5U));
}

static bool range16_output_nibble(lzrc_range16_encoder *encoder,
                                  uint8_t nibble) {
    if (!encoder->nibble_pending) {
        encoder->nibble_buffer = (uint8_t)(nibble << 4U);
        encoder->nibble_pending = true;
        return true;
    }
    if (encoder->output_size >= encoder->output_capacity) {
        encoder->failed = true;
        return false;
    }
    encoder->output[encoder->output_size] =
        (uint8_t)(encoder->nibble_buffer | nibble);
    ++encoder->output_size;
    encoder->nibble_pending = false;
    return true;
}

static uint16_t range16_bound(const lzrc_range16_encoder *encoder,
                              uint8_t probability) {
    if (encoder->use_table) {
        return lzrc_bound16_table(encoder->range, probability);
    }
    return lzrc_bound16_native(encoder->range, probability);
}

static bool range16_encode_probability(lzrc_range16_encoder *encoder,
                                       uint8_t probability,
                                       uint8_t bit) {
    uint16_t bound;
    if (encoder->failed || encoder->finished || bit > 1U) {
        encoder->failed = true;
        return false;
    }

    bound = range16_bound(encoder, probability);
    if (bit == 0U) {
        encoder->range = bound;
    } else {
        encoder->low = (uint16_t)(encoder->low + bound);
        encoder->range = (uint16_t)(encoder->range - bound);
    }

    for (;;) {
        const uint16_t sum = (uint16_t)(encoder->low + encoder->range);
        bool normalize = (uint16_t)(encoder->low ^ sum) < LZRC_RANGE16_TOP;
        if (!normalize && encoder->range < LZRC_RANGE16_BOT) {
            encoder->range =
                (uint16_t)((UINT32_C(0) - (uint32_t)encoder->low) &
                           (LZRC_RANGE16_BOT - UINT16_C(1)));
            normalize = true;
        }
        if (!normalize) {
            break;
        }
        if (encoder->range == 0U) {
            encoder->range = LZRC_RANGE16_BOT - UINT16_C(1);
        }
        if (!range16_output_nibble(encoder,
                                   (uint8_t)(encoder->low >> 12U))) {
            return false;
        }
        encoder->low = (uint16_t)(encoder->low << 4U);
        encoder->range = (uint16_t)(encoder->range << 4U);
    }
    return true;
}

void lzrc_range16_encoder_init(lzrc_range16_encoder *encoder,
                               uint8_t *output,
                               size_t output_capacity,
                               bool use_table) {
    encoder->low = 0U;
    encoder->range = UINT16_MAX;
    encoder->output = output;
    encoder->output_capacity = output_capacity;
    encoder->output_size = 0U;
    encoder->nibble_buffer = 0U;
    encoder->nibble_pending = false;
    encoder->use_table = use_table;
    encoder->failed = false;
    encoder->finished = false;
}

bool lzrc_range16_encode_bit(lzrc_range16_encoder *encoder,
                             lzrc_context *context,
                             uint8_t bit) {
    if (!range16_encode_probability(encoder, context->probability, bit)) {
        return false;
    }
    context->probability = lzrc_probability_update(context->probability, bit);
    return true;
}

bool lzrc_range16_encode_direct_bit(lzrc_range16_encoder *encoder,
                                    uint8_t bit) {
    return range16_encode_probability(encoder, LZRC_INITIAL_PROBABILITY, bit);
}

bool lzrc_range16_encoder_finish(lzrc_range16_encoder *encoder) {
    unsigned int index;
    if (encoder->failed || encoder->finished) {
        return false;
    }
    for (index = 0U; index < 4U; ++index) {
        if (!range16_output_nibble(encoder,
                                   (uint8_t)(encoder->low >> 12U))) {
            return false;
        }
        encoder->low = (uint16_t)(encoder->low << 4U);
    }
    if (encoder->nibble_pending) {
        if (encoder->output_size >= encoder->output_capacity) {
            encoder->failed = true;
            return false;
        }
        encoder->output[encoder->output_size] = encoder->nibble_buffer;
        ++encoder->output_size;
        encoder->nibble_pending = false;
    }
    encoder->finished = true;
    return true;
}

size_t lzrc_range16_encoder_size(const lzrc_range16_encoder *encoder) {
    return encoder->output_size;
}

static bool range16_input_nibble(lzrc_range16_decoder *decoder,
                                 uint8_t *nibble) {
    const size_t byte_index = decoder->nibble_index >> 1U;
    if (byte_index >= decoder->input_size) {
        decoder->failed = true;
        return false;
    }
    if ((decoder->nibble_index & 1U) == 0U) {
        *nibble = (uint8_t)(decoder->input[byte_index] >> 4U);
    } else {
        *nibble = (uint8_t)(decoder->input[byte_index] & UINT8_C(0x0F));
    }
    ++decoder->nibble_index;
    return true;
}

bool lzrc_range16_decoder_init(lzrc_range16_decoder *decoder,
                               const uint8_t *input,
                               size_t input_size,
                               bool use_table) {
    unsigned int index;
    decoder->low = 0U;
    decoder->range = UINT16_MAX;
    decoder->code = 0U;
    decoder->input = input;
    decoder->input_size = input_size;
    decoder->nibble_index = 0U;
    decoder->use_table = use_table;
    decoder->failed = false;

    for (index = 0U; index < 4U; ++index) {
        uint8_t nibble;
        if (!range16_input_nibble(decoder, &nibble)) {
            return false;
        }
        decoder->code = (uint16_t)((uint16_t)(decoder->code << 4U) | nibble);
    }
    return true;
}

static uint16_t range16_decoder_bound(const lzrc_range16_decoder *decoder,
                                      uint8_t probability) {
    if (decoder->use_table) {
        return lzrc_bound16_table(decoder->range, probability);
    }
    return lzrc_bound16_native(decoder->range, probability);
}

static bool range16_decode_probability(lzrc_range16_decoder *decoder,
                                       uint8_t probability,
                                       uint8_t *bit) {
    const uint16_t bound = range16_decoder_bound(decoder, probability);
    const uint16_t difference = (uint16_t)(decoder->code - decoder->low);
    if (decoder->failed) {
        return false;
    }

    if (difference < bound) {
        *bit = 0U;
        decoder->range = bound;
    } else {
        *bit = 1U;
        decoder->low = (uint16_t)(decoder->low + bound);
        decoder->range = (uint16_t)(decoder->range - bound);
    }

    for (;;) {
        const uint16_t sum = (uint16_t)(decoder->low + decoder->range);
        bool normalize = (uint16_t)(decoder->low ^ sum) < LZRC_RANGE16_TOP;
        if (!normalize && decoder->range < LZRC_RANGE16_BOT) {
            decoder->range =
                (uint16_t)((UINT32_C(0) - (uint32_t)decoder->low) &
                           (LZRC_RANGE16_BOT - UINT16_C(1)));
            normalize = true;
        }
        if (!normalize) {
            break;
        }
        if (decoder->range == 0U) {
            decoder->range = LZRC_RANGE16_BOT - UINT16_C(1);
        }
        {
            uint8_t nibble;
            if (!range16_input_nibble(decoder, &nibble)) {
                return false;
            }
            decoder->code =
                (uint16_t)((uint16_t)(decoder->code << 4U) | nibble);
        }
        decoder->low = (uint16_t)(decoder->low << 4U);
        decoder->range = (uint16_t)(decoder->range << 4U);
    }
    return true;
}

bool lzrc_range16_decode_bit(lzrc_range16_decoder *decoder,
                             lzrc_context *context,
                             uint8_t *bit) {
    if (!range16_decode_probability(decoder, context->probability, bit)) {
        return false;
    }
    context->probability = lzrc_probability_update(context->probability, *bit);
    return true;
}

bool lzrc_range16_decode_direct_bit(lzrc_range16_decoder *decoder,
                                    uint8_t *bit) {
    return range16_decode_probability(decoder, LZRC_INITIAL_PROBABILITY, bit);
}

static bool range32_output_byte(lzrc_range32_encoder *encoder, uint8_t byte) {
    if (encoder->output_size >= encoder->output_capacity) {
        encoder->failed = true;
        return false;
    }
    encoder->output[encoder->output_size] = byte;
    ++encoder->output_size;
    return true;
}

static bool range32_encode_probability(lzrc_range32_encoder *encoder,
                                       uint8_t probability,
                                       uint8_t bit) {
    const uint32_t bound = lzrc_bound32_native(encoder->range, probability);
    if (encoder->failed || encoder->finished || bit > 1U) {
        encoder->failed = true;
        return false;
    }

    if (bit == 0U) {
        encoder->range = bound;
    } else {
        encoder->low += bound;
        encoder->range -= bound;
    }

    for (;;) {
        const uint32_t sum = encoder->low + encoder->range;
        bool normalize = (encoder->low ^ sum) < LZRC_RANGE32_TOP;
        if (!normalize && encoder->range < LZRC_RANGE32_BOT) {
            encoder->range = (UINT32_C(0) - encoder->low) &
                             (LZRC_RANGE32_BOT - UINT32_C(1));
            normalize = true;
        }
        if (!normalize) {
            break;
        }
        if (encoder->range == 0U) {
            encoder->range = LZRC_RANGE32_BOT - UINT32_C(1);
        }
        if (!range32_output_byte(encoder, (uint8_t)(encoder->low >> 24U))) {
            return false;
        }
        encoder->low <<= 8U;
        encoder->range <<= 8U;
    }
    return true;
}

void lzrc_range32_encoder_init(lzrc_range32_encoder *encoder,
                               uint8_t *output,
                               size_t output_capacity) {
    encoder->low = 0U;
    encoder->range = UINT32_MAX;
    encoder->output = output;
    encoder->output_capacity = output_capacity;
    encoder->output_size = 0U;
    encoder->failed = false;
    encoder->finished = false;
}

bool lzrc_range32_encode_bit(lzrc_range32_encoder *encoder,
                             lzrc_context *context,
                             uint8_t bit) {
    if (!range32_encode_probability(encoder, context->probability, bit)) {
        return false;
    }
    context->probability = lzrc_probability_update(context->probability, bit);
    return true;
}

bool lzrc_range32_encode_direct_bit(lzrc_range32_encoder *encoder,
                                    uint8_t bit) {
    return range32_encode_probability(encoder, LZRC_INITIAL_PROBABILITY, bit);
}

bool lzrc_range32_encoder_finish(lzrc_range32_encoder *encoder) {
    unsigned int index;
    if (encoder->failed || encoder->finished) {
        return false;
    }
    for (index = 0U; index < 4U; ++index) {
        if (!range32_output_byte(encoder, (uint8_t)(encoder->low >> 24U))) {
            return false;
        }
        encoder->low <<= 8U;
    }
    encoder->finished = true;
    return true;
}

size_t lzrc_range32_encoder_size(const lzrc_range32_encoder *encoder) {
    return encoder->output_size;
}

static bool range32_input_byte(lzrc_range32_decoder *decoder, uint8_t *byte) {
    if (decoder->input_position >= decoder->input_size) {
        decoder->failed = true;
        return false;
    }
    *byte = decoder->input[decoder->input_position];
    ++decoder->input_position;
    return true;
}

bool lzrc_range32_decoder_init(lzrc_range32_decoder *decoder,
                               const uint8_t *input,
                               size_t input_size) {
    unsigned int index;
    decoder->low = 0U;
    decoder->range = UINT32_MAX;
    decoder->code = 0U;
    decoder->input = input;
    decoder->input_size = input_size;
    decoder->input_position = 0U;
    decoder->failed = false;

    for (index = 0U; index < 4U; ++index) {
        uint8_t byte;
        if (!range32_input_byte(decoder, &byte)) {
            return false;
        }
        decoder->code = (decoder->code << 8U) | byte;
    }
    return true;
}

static bool range32_decode_probability(lzrc_range32_decoder *decoder,
                                       uint8_t probability,
                                       uint8_t *bit) {
    const uint32_t bound = lzrc_bound32_native(decoder->range, probability);
    const uint32_t difference = decoder->code - decoder->low;
    if (decoder->failed) {
        return false;
    }

    if (difference < bound) {
        *bit = 0U;
        decoder->range = bound;
    } else {
        *bit = 1U;
        decoder->low += bound;
        decoder->range -= bound;
    }

    for (;;) {
        const uint32_t sum = decoder->low + decoder->range;
        bool normalize = (decoder->low ^ sum) < LZRC_RANGE32_TOP;
        if (!normalize && decoder->range < LZRC_RANGE32_BOT) {
            decoder->range = (UINT32_C(0) - decoder->low) &
                             (LZRC_RANGE32_BOT - UINT32_C(1));
            normalize = true;
        }
        if (!normalize) {
            break;
        }
        if (decoder->range == 0U) {
            decoder->range = LZRC_RANGE32_BOT - UINT32_C(1);
        }
        {
            uint8_t byte;
            if (!range32_input_byte(decoder, &byte)) {
                return false;
            }
            decoder->code = (decoder->code << 8U) | byte;
        }
        decoder->low <<= 8U;
        decoder->range <<= 8U;
    }
    return true;
}

bool lzrc_range32_decode_bit(lzrc_range32_decoder *decoder,
                             lzrc_context *context,
                             uint8_t *bit) {
    if (!range32_decode_probability(decoder, context->probability, bit)) {
        return false;
    }
    context->probability = lzrc_probability_update(context->probability, *bit);
    return true;
}

bool lzrc_range32_decode_direct_bit(lzrc_range32_decoder *decoder,
                                    uint8_t *bit) {
    return range32_decode_probability(decoder, LZRC_INITIAL_PROBABILITY, bit);
}

bool lzrc_range_encoder_init(lzrc_range_encoder *encoder,
                             unsigned int profile,
                             uint8_t *output,
                             size_t output_capacity) {
    if (profile <= 2U) {
        encoder->kind = LZRC_RANGE_KIND_16;
        lzrc_range16_encoder_init(&encoder->state.range16, output,
                                  output_capacity, profile == 0U);
        return true;
    }
    if (profile <= 4U) {
        encoder->kind = LZRC_RANGE_KIND_32;
        lzrc_range32_encoder_init(&encoder->state.range32, output,
                                  output_capacity);
        return true;
    }
    encoder->kind = LZRC_RANGE_KIND_INVALID;
    return false;
}

bool lzrc_range_encoder_encode_bit(lzrc_range_encoder *encoder,
                                   lzrc_context *context,
                                   uint8_t bit) {
    if (encoder->kind == LZRC_RANGE_KIND_16) {
        return lzrc_range16_encode_bit(&encoder->state.range16, context, bit);
    }
    if (encoder->kind == LZRC_RANGE_KIND_32) {
        return lzrc_range32_encode_bit(&encoder->state.range32, context, bit);
    }
    return false;
}

bool lzrc_range_encoder_encode_direct_bit(lzrc_range_encoder *encoder,
                                          uint8_t bit) {
    if (encoder->kind == LZRC_RANGE_KIND_16) {
        return lzrc_range16_encode_direct_bit(&encoder->state.range16, bit);
    }
    if (encoder->kind == LZRC_RANGE_KIND_32) {
        return lzrc_range32_encode_direct_bit(&encoder->state.range32, bit);
    }
    return false;
}

bool lzrc_range_encoder_finish(lzrc_range_encoder *encoder) {
    if (encoder->kind == LZRC_RANGE_KIND_16) {
        return lzrc_range16_encoder_finish(&encoder->state.range16);
    }
    if (encoder->kind == LZRC_RANGE_KIND_32) {
        return lzrc_range32_encoder_finish(&encoder->state.range32);
    }
    return false;
}

size_t lzrc_range_encoder_size(const lzrc_range_encoder *encoder) {
    if (encoder->kind == LZRC_RANGE_KIND_16) {
        return lzrc_range16_encoder_size(&encoder->state.range16);
    }
    if (encoder->kind == LZRC_RANGE_KIND_32) {
        return lzrc_range32_encoder_size(&encoder->state.range32);
    }
    return 0U;
}

bool lzrc_range_decoder_init(lzrc_range_decoder *decoder,
                             unsigned int profile,
                             const uint8_t *input,
                             size_t input_size) {
    if (profile <= 2U) {
        decoder->kind = LZRC_RANGE_KIND_16;
        return lzrc_range16_decoder_init(&decoder->state.range16, input,
                                         input_size, profile == 0U);
    }
    if (profile <= 4U) {
        decoder->kind = LZRC_RANGE_KIND_32;
        return lzrc_range32_decoder_init(&decoder->state.range32, input,
                                         input_size);
    }
    decoder->kind = LZRC_RANGE_KIND_INVALID;
    return false;
}

bool lzrc_range_decoder_decode_bit(lzrc_range_decoder *decoder,
                                   lzrc_context *context,
                                   uint8_t *bit) {
    if (decoder->kind == LZRC_RANGE_KIND_16) {
        return lzrc_range16_decode_bit(&decoder->state.range16, context, bit);
    }
    if (decoder->kind == LZRC_RANGE_KIND_32) {
        return lzrc_range32_decode_bit(&decoder->state.range32, context, bit);
    }
    return false;
}

bool lzrc_range_decoder_decode_direct_bit(lzrc_range_decoder *decoder,
                                          uint8_t *bit) {
    if (decoder->kind == LZRC_RANGE_KIND_16) {
        return lzrc_range16_decode_direct_bit(&decoder->state.range16, bit);
    }
    if (decoder->kind == LZRC_RANGE_KIND_32) {
        return lzrc_range32_decode_direct_bit(&decoder->state.range32, bit);
    }
    return false;
}
