#ifndef LZRC_CARC_H
#define LZRC_CARC_H

#include "range_coder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum lzrc_token_type {
    LZRC_TOKEN_LITERAL = 0,
    LZRC_TOKEN_MATCH = 1
} lzrc_token_type;

typedef struct lzrc_token {
    lzrc_token_type type;
    uint8_t literal;
    uint16_t length;
    uint32_t offset;
} lzrc_token;

typedef struct lzrc_carc_model {
    unsigned int profile;
    bool previous_was_match;
    lzrc_context *flags;
    lzrc_context *literal;
    lzrc_context *length_mini;
    lzrc_context *length_more;
    lzrc_context *length_extend;
    lzrc_context *offsets[5];
} lzrc_carc_model;

typedef struct lzrc_carc_encoder {
    lzrc_range_encoder range;
    lzrc_carc_model model;
} lzrc_carc_encoder;

typedef struct lzrc_carc_decoder {
    lzrc_range_decoder range;
    lzrc_carc_model model;
} lzrc_carc_decoder;

lzrc_token lzrc_token_literal(uint8_t value);
lzrc_token lzrc_token_match(uint16_t length, uint32_t offset);
size_t lzrc_carc_context_count(unsigned int profile);

bool lzrc_carc_encoder_init(lzrc_carc_encoder *encoder,
                            unsigned int profile,
                            uint8_t *output,
                            size_t output_capacity,
                            lzrc_context *context_storage,
                            size_t context_count);
bool lzrc_carc_encode_token(lzrc_carc_encoder *encoder,
                            const lzrc_token *token);
bool lzrc_carc_encoder_finish(lzrc_carc_encoder *encoder);
size_t lzrc_carc_encoder_size(const lzrc_carc_encoder *encoder);

bool lzrc_carc_decoder_init(lzrc_carc_decoder *decoder,
                            unsigned int profile,
                            const uint8_t *input,
                            size_t input_size,
                            lzrc_context *context_storage,
                            size_t context_count);
bool lzrc_carc_decode_token(lzrc_carc_decoder *decoder, lzrc_token *token);

#ifdef __cplusplus
}
#endif

#endif
