#include "bwt.h"
#include "bit_tree.h"

#include <stdlib.h>
#include <string.h>

void lzrc_mtf_forward(const uint8_t *input, size_t size, uint8_t *ranks) {
    uint8_t alphabet[256];
    size_t i;
    unsigned int rank;
    for (rank = 0U; rank < 256U; ++rank) alphabet[rank] = (uint8_t)rank;
    for (i = 0U; i < size; ++i) {
        const uint8_t value = input[i];
        for (rank = 0U; alphabet[rank] != value; ++rank) { }
        ranks[i] = (uint8_t)rank;
        memmove(alphabet + 1U, alphabet, rank);
        alphabet[0] = value;
    }
}

void lzrc_mtf_inverse(const uint8_t *ranks, size_t size, uint8_t *output) {
    uint8_t alphabet[256];
    size_t i;
    unsigned int rank;
    for (rank = 0U; rank < 256U; ++rank) alphabet[rank] = (uint8_t)rank;
    for (i = 0U; i < size; ++i) {
        const uint8_t index = ranks[i], value = alphabet[index];
        output[i] = value;
        memmove(alphabet + 1U, alphabet, index);
        alphabet[0] = value;
    }
}

static bool encode_run(lzrc_range_encoder *encoder, size_t run) {
    size_t i;
    /* The zero flag already represents the first zero. Always terminate. */
    for (i = 1U; i < run; ++i) {
        if (!lzrc_range32_encode_fixed_bit(&encoder->state.range32, 255U, 0U))
            return false;
    }
    return lzrc_range32_encode_fixed_bit(&encoder->state.range32, 255U, 1U);
}

static bool decode_run(lzrc_range_decoder *decoder, size_t limit, size_t *run) {
    *run = 1U;
    for (;;) {
        uint8_t bit;
        if (!lzrc_range32_decode_fixed_bit(&decoder->state.range32, 255U, &bit))
            return false;
        if (bit != 0U) return true;
        if (*run >= limit) return false;
        ++*run;
    }
}

static lzrc_result encode_mtf(const uint8_t *last, size_t size,
                              uint8_t *output, size_t capacity, size_t *written) {
    lzrc_range_encoder encoder;
    lzrc_context flag = {128U}, tree[255];
    uint8_t *ranks = (uint8_t *)malloc(size);
    size_t i = 0U;
    bool ok = true;
    if (ranks == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    lzrc_mtf_forward(last, size, ranks);
    lzrc_contexts_init(tree, 255U);
    (void)lzrc_range_encoder_init(&encoder, 4U, output, capacity);
    while (i < size && ok) {
        const uint8_t value = ranks[i];
        ok = lzrc_range_encoder_encode_bit(&encoder, &flag, value == 0U ? 0U : 1U);
        if (value == 0U) {
            size_t end = i + 1U;
            while (end < size && ranks[end] == 0U) ++end;
            if (ok) ok = encode_run(&encoder, end - i);
            i = end;
        } else {
            if (ok) ok = lzrc_bit_tree_encode(&encoder, tree, 8U, value);
            ++i;
        }
    }
    free(ranks);
    if (!ok || !lzrc_range_encoder_finish(&encoder)) return LZRC_ERROR_OUTPUT_TOO_SMALL;
    *written = lzrc_range_encoder_size(&encoder);
    return LZRC_OK;
}

static lzrc_result decode_mtf(const uint8_t *input, size_t input_size,
                              uint8_t *last, size_t size) {
    lzrc_range_decoder decoder;
    lzrc_context flag = {128U}, tree[255];
    size_t i = 0U;
    lzrc_contexts_init(tree, 255U);
    if (!lzrc_range_decoder_init(&decoder, 4U, input, input_size))
        return LZRC_ERROR_CORRUPT_INPUT;
    while (i < size) {
        uint8_t bit;
        if (!lzrc_range_decoder_decode_bit(&decoder, &flag, &bit))
            return LZRC_ERROR_CORRUPT_INPUT;
        if (bit == 0U) {
            size_t run;
            if (!decode_run(&decoder, size - i, &run)) return LZRC_ERROR_CORRUPT_INPUT;
            memset(last + i, 0, run);
            i += run;
        } else {
            uint32_t value;
            if (!lzrc_bit_tree_decode(&decoder, tree, 8U, &value) || value == 0U)
                return LZRC_ERROR_CORRUPT_INPUT;
            last[i++] = (uint8_t)value;
        }
    }
    if (decoder.state.range32.input_position != input_size)
        return LZRC_ERROR_CORRUPT_INPUT;
    lzrc_mtf_inverse(last, size, last);
    return LZRC_OK;
}

lzrc_result lzrc_bwt_encode_block(unsigned int stage, const uint8_t *last,
                                 size_t size, uint8_t *output,
                                 size_t capacity, size_t *written) {
    lzrc_range_encoder encoder;
    const size_t count = stage == 2U ? 256U * 255U : 255U;
    lzrc_context *contexts;
    uint8_t previous = 0U;
    size_t i;
    if (stage >= 3U) return encode_mtf(last, size, output, capacity, written);
    contexts = (lzrc_context *)malloc(count * sizeof(lzrc_context));
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
    if (stage >= 3U) return decode_mtf(input, input_size, last, size);
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
