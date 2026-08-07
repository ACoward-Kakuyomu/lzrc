#ifndef LZRC_RANGE_CODER_H
#define LZRC_RANGE_CODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lzrc_context {
    uint8_t probability;
} lzrc_context;

typedef struct lzrc_range16_encoder {
    uint16_t low;
    uint16_t range;
    uint8_t *output;
    size_t output_capacity;
    size_t output_size;
    uint8_t nibble_buffer;
    bool nibble_pending;
    bool use_table;
    bool failed;
    bool finished;
} lzrc_range16_encoder;

typedef struct lzrc_range16_decoder {
    uint16_t low;
    uint16_t range;
    uint16_t code;
    const uint8_t *input;
    size_t input_size;
    size_t nibble_index;
    bool use_table;
    bool failed;
} lzrc_range16_decoder;

typedef struct lzrc_range32_encoder {
    uint32_t low;
    uint32_t range;
    uint8_t *output;
    size_t output_capacity;
    size_t output_size;
    bool failed;
    bool finished;
} lzrc_range32_encoder;

typedef struct lzrc_range32_decoder {
    uint32_t low;
    uint32_t range;
    uint32_t code;
    const uint8_t *input;
    size_t input_size;
    size_t input_position;
    bool failed;
} lzrc_range32_decoder;

typedef enum lzrc_range_kind {
    LZRC_RANGE_KIND_INVALID = 0,
    LZRC_RANGE_KIND_16 = 16,
    LZRC_RANGE_KIND_32 = 32
} lzrc_range_kind;

typedef struct lzrc_range_encoder {
    lzrc_range_kind kind;
    union {
        lzrc_range16_encoder range16;
        lzrc_range32_encoder range32;
    } state;
} lzrc_range_encoder;

typedef struct lzrc_range_decoder {
    lzrc_range_kind kind;
    union {
        lzrc_range16_decoder range16;
        lzrc_range32_decoder range32;
    } state;
} lzrc_range_decoder;

void lzrc_contexts_init(lzrc_context *contexts, size_t count);
uint8_t lzrc_probability_update(uint8_t probability, uint8_t bit);

void lzrc_range16_encoder_init(lzrc_range16_encoder *encoder,
                               uint8_t *output,
                               size_t output_capacity,
                               bool use_table);
bool lzrc_range16_encode_bit(lzrc_range16_encoder *encoder,
                             lzrc_context *context,
                             uint8_t bit);
bool lzrc_range16_encode_direct_bit(lzrc_range16_encoder *encoder,
                                    uint8_t bit);
bool lzrc_range16_encoder_finish(lzrc_range16_encoder *encoder);
size_t lzrc_range16_encoder_size(const lzrc_range16_encoder *encoder);

bool lzrc_range16_decoder_init(lzrc_range16_decoder *decoder,
                               const uint8_t *input,
                               size_t input_size,
                               bool use_table);
bool lzrc_range16_decode_bit(lzrc_range16_decoder *decoder,
                             lzrc_context *context,
                             uint8_t *bit);
bool lzrc_range16_decode_direct_bit(lzrc_range16_decoder *decoder,
                                    uint8_t *bit);

void lzrc_range32_encoder_init(lzrc_range32_encoder *encoder,
                               uint8_t *output,
                               size_t output_capacity);
bool lzrc_range32_encode_bit(lzrc_range32_encoder *encoder,
                             lzrc_context *context,
                             uint8_t bit);
bool lzrc_range32_encode_direct_bit(lzrc_range32_encoder *encoder,
                                    uint8_t bit);
bool lzrc_range32_encoder_finish(lzrc_range32_encoder *encoder);
size_t lzrc_range32_encoder_size(const lzrc_range32_encoder *encoder);

bool lzrc_range32_decoder_init(lzrc_range32_decoder *decoder,
                               const uint8_t *input,
                               size_t input_size);
bool lzrc_range32_decode_bit(lzrc_range32_decoder *decoder,
                             lzrc_context *context,
                             uint8_t *bit);
bool lzrc_range32_decode_direct_bit(lzrc_range32_decoder *decoder,
                                    uint8_t *bit);

bool lzrc_range_encoder_init(lzrc_range_encoder *encoder,
                             unsigned int profile,
                             uint8_t *output,
                             size_t output_capacity);
bool lzrc_range_encoder_encode_bit(lzrc_range_encoder *encoder,
                                   lzrc_context *context,
                                   uint8_t bit);
bool lzrc_range_encoder_encode_direct_bit(lzrc_range_encoder *encoder,
                                          uint8_t bit);
bool lzrc_range_encoder_finish(lzrc_range_encoder *encoder);
size_t lzrc_range_encoder_size(const lzrc_range_encoder *encoder);

bool lzrc_range_decoder_init(lzrc_range_decoder *decoder,
                             unsigned int profile,
                             const uint8_t *input,
                             size_t input_size);
bool lzrc_range_decoder_decode_bit(lzrc_range_decoder *decoder,
                                   lzrc_context *context,
                                   uint8_t *bit);
bool lzrc_range_decoder_decode_direct_bit(lzrc_range_decoder *decoder,
                                          uint8_t *bit);

#ifdef __cplusplus
}
#endif

#endif
