#ifndef LZRC_LZSS_H
#define LZRC_LZSS_H

#include "carc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lzrc_profile_parameters {
    uint32_t window_size;
    uint16_t maximum_match_length;
} lzrc_profile_parameters;

bool lzrc_profile_get(unsigned int profile,
                      lzrc_profile_parameters *parameters);
bool lzrc_lzss_tokenize(unsigned int profile,
                        const uint8_t *input,
                        size_t input_size,
                        lzrc_token *tokens,
                        size_t token_capacity,
                        size_t *token_count);
bool lzrc_lzss_detokenize(unsigned int profile,
                          const lzrc_token *tokens,
                          size_t token_count,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
