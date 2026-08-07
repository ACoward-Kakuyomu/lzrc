#include "lzrc/lzrc.h"

#include "carc.h"
#include "lzss.h"

#include <stdint.h>
#include <stdlib.h>

lzrc_result lzrc_compress_bound(size_t input_size, size_t *bound) {
    if (bound == NULL || input_size > (SIZE_MAX - 16U) / 16U) {
        return LZRC_ERROR_INVALID_ARGUMENT;
    }
    *bound = input_size * 16U + 16U;
    return LZRC_OK;
}

lzrc_result lzrc_compress(unsigned int profile,
                          const uint8_t *input,
                          size_t input_size,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_size) {
    lzrc_token *tokens = NULL;
    lzrc_context *contexts = NULL;
    size_t token_count = 0U;
    const size_t context_count = lzrc_carc_context_count(profile);
    lzrc_carc_encoder encoder;
    size_t index;
    lzrc_result result = LZRC_OK;

    if (output_size == NULL || (input == NULL && input_size != 0U) ||
        (output == NULL && output_capacity != 0U)) {
        return LZRC_ERROR_INVALID_ARGUMENT;
    }
    *output_size = 0U;
    if (profile > 4U) {
        return LZRC_ERROR_INVALID_PROFILE;
    }
    if (input_size > SIZE_MAX / sizeof(*tokens)) {
        return LZRC_ERROR_INVALID_ARGUMENT;
    }

    if (input_size != 0U) {
        tokens = (lzrc_token *)malloc(input_size * sizeof(*tokens));
        if (tokens == NULL) {
            return LZRC_ERROR_OUT_OF_MEMORY;
        }
    }
    contexts = (lzrc_context *)malloc(context_count * sizeof(*contexts));
    if (contexts == NULL) {
        free(tokens);
        return LZRC_ERROR_OUT_OF_MEMORY;
    }
    if (!lzrc_lzss_tokenize(profile, input, input_size, tokens, input_size,
                            &token_count)) {
        result = LZRC_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    if (!lzrc_carc_encoder_init(&encoder, profile, output, output_capacity,
                                contexts, context_count)) {
        result = LZRC_ERROR_INVALID_ARGUMENT;
        goto cleanup;
    }
    for (index = 0U; index < token_count; ++index) {
        if (!lzrc_carc_encode_token(&encoder, &tokens[index])) {
            result = LZRC_ERROR_OUTPUT_TOO_SMALL;
            goto cleanup;
        }
    }
    if (!lzrc_carc_encoder_finish(&encoder)) {
        result = LZRC_ERROR_OUTPUT_TOO_SMALL;
        goto cleanup;
    }
    *output_size = lzrc_carc_encoder_size(&encoder);

cleanup:
    free(contexts);
    free(tokens);
    return result;
}

lzrc_result lzrc_decompress(unsigned int profile,
                            const uint8_t *input,
                            size_t input_size,
                            size_t expected_output_size,
                            uint8_t *output,
                            size_t output_capacity,
                            size_t *output_size) {
    lzrc_context *contexts;
    const size_t context_count = lzrc_carc_context_count(profile);
    lzrc_carc_decoder decoder;
    size_t produced = 0U;

    if (output_size == NULL || (input == NULL && input_size != 0U) ||
        (output == NULL && output_capacity != 0U)) {
        return LZRC_ERROR_INVALID_ARGUMENT;
    }
    *output_size = 0U;
    if (profile > 4U) {
        return LZRC_ERROR_INVALID_PROFILE;
    }
    if (output_capacity < expected_output_size) {
        return LZRC_ERROR_OUTPUT_TOO_SMALL;
    }

    contexts = (lzrc_context *)malloc(context_count * sizeof(*contexts));
    if (contexts == NULL) {
        return LZRC_ERROR_OUT_OF_MEMORY;
    }
    if (!lzrc_carc_decoder_init(&decoder, profile, input, input_size, contexts,
                                context_count)) {
        free(contexts);
        return LZRC_ERROR_CORRUPT_INPUT;
    }

    while (produced < expected_output_size) {
        lzrc_token token;
        if (!lzrc_carc_decode_token(&decoder, &token)) {
            free(contexts);
            return LZRC_ERROR_CORRUPT_INPUT;
        }
        if (token.type == LZRC_TOKEN_LITERAL) {
            output[produced] = token.literal;
            ++produced;
        } else {
            size_t index;
            if (token.offset > produced ||
                token.length > expected_output_size - produced) {
                free(contexts);
                return LZRC_ERROR_CORRUPT_INPUT;
            }
            for (index = 0U; index < token.length; ++index) {
                output[produced] = output[produced - token.offset];
                ++produced;
            }
        }
    }

    free(contexts);
    *output_size = produced;
    return LZRC_OK;
}

const char *lzrc_result_string(lzrc_result result) {
    switch (result) {
        case LZRC_OK:
            return "success";
        case LZRC_ERROR_INVALID_ARGUMENT:
            return "invalid argument";
        case LZRC_ERROR_INVALID_PROFILE:
            return "invalid profile";
        case LZRC_ERROR_OUTPUT_TOO_SMALL:
            return "output buffer too small";
        case LZRC_ERROR_CORRUPT_INPUT:
            return "corrupt or truncated input";
        case LZRC_ERROR_OUT_OF_MEMORY:
            return "out of memory";
        default:
            return "unknown error";
    }
}
