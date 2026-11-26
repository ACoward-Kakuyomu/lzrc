#ifndef LZRC_BWT_H
#define LZRC_BWT_H

#include "lzrc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* W1..W8 select multiples of 128 KiB. Containers identify their own stage. */
#define LZRC_BWT_STAGE 1U
#define LZRC_BWT_BLOCK_UNIT 131072U

lzrc_result lzrc_bwt_compress_bound(size_t input_size, unsigned int blocks,
                                   size_t *bound);
lzrc_result lzrc_bwt_compress(unsigned int blocks,
                             const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_size);
/* expected_output_size must agree with the size recorded in the container. */
lzrc_result lzrc_bwt_decompress(const uint8_t *input, size_t input_size,
                               size_t expected_output_size,
                               uint8_t *output, size_t output_capacity,
                               size_t *output_size);

#ifdef __cplusplus
}
#endif
#endif
