#ifndef LZRC_BIT_TREE_H
#define LZRC_BIT_TREE_H

#include "range_coder.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lzrc_bit_tree_encode(lzrc_range_encoder *encoder,
                          lzrc_context *contexts,
                          unsigned int bit_width,
                          uint32_t value);
bool lzrc_bit_tree_decode(lzrc_range_decoder *decoder,
                          lzrc_context *contexts,
                          unsigned int bit_width,
                          uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif
