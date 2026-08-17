#ifndef LZRC_LZSS_MATCHFINDER_INTERNAL_H
#define LZRC_LZSS_MATCHFINDER_INTERNAL_H

#include "lzss.h"
#include "scapegoat_matchfinder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lzrc_lzss_matchfinder_statistics {
    uint64_t hash_candidate_byte_comparison_count;
    uint64_t hash_visited_node_count;
    lzrc_sg_statistics scapegoat;
} lzrc_lzss_matchfinder_statistics;

bool lzrc_lzss_tokenize_instrumented(
    unsigned int profile,
    const uint8_t *input,
    size_t input_size,
    lzrc_token *tokens,
    size_t token_capacity,
    size_t *token_count,
    bool use_hybrid_scapegoat,
    lzrc_lzss_matchfinder_statistics *statistics);

#ifdef __cplusplus
}
#endif

#endif
