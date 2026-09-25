#include "lzss.h"

#include <stdint.h>
#include <stdlib.h>

#define LZRC_FAST_MAX_CANDIDATES(PROFILE) (64U*(PROFILE))

static const lzrc_profile_parameters kProfiles[5] = {
    {UINT32_C(256), UINT16_C(18)},
    {UINT32_C(4096), UINT16_C(274)},
    {UINT32_C(65536), UINT16_C(274)},
    {UINT32_C(1048576), UINT16_C(274)},
    {UINT32_C(16777216), UINT16_C(530)}};

bool lzrc_profile_get(unsigned int profile,
                      lzrc_profile_parameters *parameters) {
    if (profile > 4U || parameters == NULL) {
        return false;
    }
    *parameters = kProfiles[profile];
    return true;
}

uint32_t lzrc_lzss_estimated_literal_bits(uint16_t length) {
    return (uint32_t)length * UINT32_C(9);
}

uint32_t lzrc_lzss_estimated_match_bits(uint16_t length, uint32_t offset) {
    uint32_t length_bits;
    uint32_t offset_bits;
    if (length < 3U || length > 530U || offset == 0U ||
        offset > UINT32_C(16777216)) {
        return UINT32_MAX;
    }
    if (length <= 18U) {
        length_bits = 5U;
    } else if (length <= 274U) {
        length_bits = 10U;
    } else {
        length_bits = 11U;
    }

    if (offset <= UINT32_C(256)) {
        offset_bits = 9U;
    } else if (offset <= UINT32_C(4096)) {
        offset_bits = 14U;
    } else if (offset <= UINT32_C(65536)) {
        offset_bits = 19U;
    } else if (offset <= UINT32_C(1048576)) {
        offset_bits = 24U;
    } else {
        offset_bits = 29U;
    }
    return UINT32_C(1) + length_bits + offset_bits;
}

static bool emit_token(lzrc_token *tokens,
                       size_t token_capacity,
                       size_t *token_count,
                       lzrc_token token) {
    if (*token_count >= token_capacity) {
        return false;
    }
    tokens[*token_count] = token;
    ++(*token_count);
    return true;
}

static bool tokenize_profile0(const uint8_t *input,
                              size_t input_size,
                              lzrc_token *tokens,
                              size_t token_capacity,
                              size_t *token_count) {
    size_t position = 0U;
    while (position < input_size) {
        const size_t window_start =
            (position > kProfiles[0].window_size)
                ? position - kProfiles[0].window_size
                : 0U;
        size_t scan = window_start;
        size_t match_length = 0U;
        size_t match_offset = 0U;

        while (scan < position) {
            size_t candidate;
            size_t length;
            while (scan < position && input[scan] != input[position]) {
                ++scan;
            }
            if (scan >= position) {
                break;
            }
            candidate = scan;
            length = 0U;
            while (length < kProfiles[0].maximum_match_length &&
                   position + length < input_size &&
                   input[candidate + length] == input[position + length]) {
                ++length;
            }
            if (length >= 3U) {
                match_length = length;
                match_offset = position - candidate;
                break;
            }

            /* Skip the mismatch position as well as the verified equal bytes. */
            scan = candidate + length + 1U;
        }

        if (match_length >= 3U) {
            if (!emit_token(tokens, token_capacity, token_count,
                            lzrc_token_match((uint16_t)match_length,
                                             (uint32_t)match_offset))) {
                return false;
            }
            position += match_length;
        } else {
            if (!emit_token(tokens, token_capacity, token_count,
                            lzrc_token_literal(input[position]))) {
                return false;
            }
            ++position;
        }
    }
    return true;
}

static uint32_t hash3(const uint8_t *input,
                      size_t position,
                      uint32_t hash_mask) {
    uint32_t hash = input[position];
    hash = hash * UINT32_C(251) + input[position + 1U];
    hash = hash * UINT32_C(251) + input[position + 2U];
    return hash & hash_mask;
}

static void insert_hash_positions(const uint8_t *input,
                                  size_t input_size,
                                  size_t begin,
                                  size_t end,
                                  uint32_t *heads,
                                  uint32_t *previous,
                                  uint32_t hash_mask) {
    size_t position;
    for (position = begin; position < end; ++position) {
        if (input_size - position >= 3U) {
            const uint32_t hash = hash3(input, position, hash_mask);
            previous[position] = heads[hash];
            heads[hash] = (uint32_t)position;
        } else {
            previous[position] = UINT32_MAX;
        }
    }
}

static bool tokenize_hashed(unsigned int profile,
                            lzrc_lzss_search_mode mode,
                            const uint8_t *input,
                            size_t input_size,
                            lzrc_token *tokens,
                            size_t token_capacity,
                            size_t *token_count) {
    const lzrc_profile_parameters parameters = kProfiles[profile];
    const uint32_t hash_count = (profile == 1U) ? UINT32_C(4096)
                                                : UINT32_C(65536);
    const uint32_t hash_mask = hash_count - UINT32_C(1);
    uint32_t *heads;
    uint32_t *previous;
    size_t position = 0U;
    size_t index;
    bool result = true;

    if (input_size > UINT32_MAX ||
        input_size > SIZE_MAX / sizeof(*previous)) {
        return false;
    }
    heads = (uint32_t *)malloc((size_t)hash_count * sizeof(*heads));
    previous = (uint32_t *)malloc(input_size * sizeof(*previous));
    if (heads == NULL || (previous == NULL && input_size != 0U)) {
        free(heads);
        free(previous);
        return false;
    }
    for (index = 0U; index < hash_count; ++index) {
        heads[index] = UINT32_MAX;
    }

    while (position < input_size) {
        size_t best_length = 0U;
        size_t longest_length = 0U;
        size_t candidates_examined = 0U;
        uint32_t best_offset = 0U;
        uint32_t best_savings = 0U;
        if (input_size - position >= 3U) {
            const uint32_t hash = hash3(input, position, hash_mask);
            uint32_t candidate = heads[hash];
            while (candidate != UINT32_MAX) {
                const size_t candidate_position = candidate;
                const size_t offset = position - candidate_position;
                const size_t maximum_possible_length =
                    ((input_size - position) <
                     parameters.maximum_match_length)
                        ? (input_size - position)
                        : parameters.maximum_match_length;
                size_t length = 0U;
                if (offset > parameters.window_size) {
                    break;
                }
                if (mode == LZRC_LZSS_SEARCH_FAST) {
                    if (candidates_examined >= LZRC_FAST_MAX_CANDIDATES(profile)) {
                        break;
                    }
                    ++candidates_examined;
                }
                if (longest_length >= 3U &&
                    longest_length < maximum_possible_length &&
                    input[candidate_position + longest_length] !=
                        input[position + longest_length]) {
                    candidate = previous[candidate_position];
                    continue;
                }
                while (length < parameters.maximum_match_length &&
                       position + length < input_size &&
                       input[candidate_position + length] ==
                           input[position + length]) {
                    ++length;
                }
                if (length > longest_length) {
                    longest_length = length;
                }
                if (length >= 3U) {
                    if (profile == 1U && length > best_length) {
                        best_length = length;
                        best_offset = (uint32_t)offset;
                        if (best_length == parameters.maximum_match_length) {
                            break;
                        }
                    } else if (profile >= 2U) {
                        const uint32_t match_bits =
                            lzrc_lzss_estimated_match_bits((uint16_t)length,
                                                           (uint32_t)offset);
                        const uint32_t literal_bits =
                            lzrc_lzss_estimated_literal_bits((uint16_t)length);
                        const uint32_t savings =
                            (literal_bits > match_bits)
                                ? literal_bits - match_bits
                                : 0U;
                        if (savings > best_savings ||
                            (savings == best_savings && savings != 0U &&
                             (length > best_length ||
                              (length == best_length &&
                               offset < best_offset)))) {
                            best_length = length;
                            best_offset = (uint32_t)offset;
                            best_savings = savings;
                            if (best_length ==
                                parameters.maximum_match_length) {
                                break;
                            }
                        }
                    }
                }
                candidate = previous[candidate_position];
            }
        }

        if (best_length >= 3U) {
            if (!emit_token(tokens, token_capacity, token_count,
                            lzrc_token_match((uint16_t)best_length,
                                             best_offset))) {
                result = false;
                break;
            }
            insert_hash_positions(input, input_size, position,
                                  position + best_length, heads, previous,
                                  hash_mask);
            position += best_length;
        } else {
            if (!emit_token(tokens, token_capacity, token_count,
                            lzrc_token_literal(input[position]))) {
                result = false;
                break;
            }
            insert_hash_positions(input, input_size, position, position + 1U,
                                  heads, previous, hash_mask);
            ++position;
        }
    }

    free(previous);
    free(heads);
    return result;
}

bool lzrc_lzss_tokenize(unsigned int profile,
                        const uint8_t *input,
                        size_t input_size,
                        lzrc_token *tokens,
                        size_t token_capacity,
                        size_t *token_count) {
    return lzrc_lzss_tokenize_with_mode(
        profile, LZRC_LZSS_SEARCH_ORIGINAL, input, input_size, tokens,
        token_capacity, token_count);
}

bool lzrc_lzss_tokenize_with_mode(unsigned int profile,
                                  lzrc_lzss_search_mode mode,
                                  const uint8_t *input,
                                  size_t input_size,
                                  lzrc_token *tokens,
                                  size_t token_capacity,
                                  size_t *token_count) {
    if (profile > 4U || token_count == NULL ||
        (mode != LZRC_LZSS_SEARCH_ORIGINAL &&
         mode != LZRC_LZSS_SEARCH_FAST) ||
        (mode == LZRC_LZSS_SEARCH_FAST && profile == 0U) ||
        (input == NULL && input_size != 0U) ||
        (tokens == NULL && token_capacity != 0U)) {
        return false;
    }
    *token_count = 0U;
    if (input_size == 0U) {
        return true;
    }
    if (tokens == NULL) {
        return false;
    }
    if (profile == 0U) {
        return tokenize_profile0(input, input_size, tokens, token_capacity,
                                 token_count);
    }
    return tokenize_hashed(profile, mode, input, input_size, tokens,
                           token_capacity, token_count);
}

static bool match_is_valid(unsigned int profile, const lzrc_token *token) {
    return token->type == LZRC_TOKEN_MATCH && token->length >= 3U &&
           token->length <= kProfiles[profile].maximum_match_length &&
           token->offset >= 1U &&
           token->offset <= kProfiles[profile].window_size;
}

bool lzrc_lzss_detokenize(unsigned int profile,
                          const lzrc_token *tokens,
                          size_t token_count,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_size) {
    size_t token_index;
    size_t produced = 0U;
    if (profile > 4U || output_size == NULL ||
        (tokens == NULL && token_count != 0U) ||
        (output == NULL && output_capacity != 0U)) {
        return false;
    }
    *output_size = 0U;
    for (token_index = 0U; token_index < token_count; ++token_index) {
        const lzrc_token *token = &tokens[token_index];
        if (token->type == LZRC_TOKEN_LITERAL) {
            if (produced >= output_capacity) {
                return false;
            }
            output[produced] = token->literal;
            ++produced;
        } else {
            size_t index;
            if (!match_is_valid(profile, token) || token->offset > produced ||
                token->length > output_capacity - produced) {
                return false;
            }
            for (index = 0U; index < token->length; ++index) {
                output[produced] = output[produced - token->offset];
                ++produced;
            }
        }
    }
    *output_size = produced;
    return true;
}
