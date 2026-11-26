#ifndef LZRC_BWT_INTERNAL_H
#define LZRC_BWT_INTERNAL_H

#include "lzrc/bwt.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nonempty blocks only, at most 1 MiB. Input/output must not overlap. */
/* MTF supports in-place operation; initial alphabet is 0..255. */
void lzrc_mtf_forward(const uint8_t *input, size_t size, uint8_t *ranks);
void lzrc_mtf_inverse(const uint8_t *ranks, size_t size, uint8_t *output);
/* Valid zero-run lengths are 1..8*LZRC_BWT_BLOCK_UNIT. */
bool lzrc_bwt_use_gamma(size_t run);
lzrc_result lzrc_bwt_forward(const uint8_t *input, size_t size,
                            uint8_t *last, uint32_t *primary);
lzrc_result lzrc_bwt_inverse(const uint8_t *last, size_t size,
                            uint32_t primary, uint8_t *output);
lzrc_result lzrc_bwt_encode_block(unsigned int stage, const uint8_t *last,
                                 size_t size, uint8_t *output,
                                 size_t capacity, size_t *written);
lzrc_result lzrc_bwt_decode_block(unsigned int stage, const uint8_t *input,
                                 size_t input_size, uint8_t *last, size_t size);

#ifdef __cplusplus
}
#endif

#endif
