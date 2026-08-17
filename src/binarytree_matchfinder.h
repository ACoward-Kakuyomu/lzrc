#ifndef LZRC_BINARYTREE_MATCHFINDER_H
#define LZRC_BINARYTREE_MATCHFINDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lzrc_sg_match {
    uint16_t length;
    uint32_t offset;
} lzrc_sg_match;

typedef struct lzrc_sg_statistics {
    uint64_t candidate_byte_comparison_count;
    uint64_t visited_node_count;
    uint64_t insert_count;
    uint64_t delete_count;
    uint64_t subtree_rebuild_count;
    uint64_t full_rebuild_count;
    uint64_t rebuilt_node_count;
    uint64_t fallback_bucket_count;
    uint32_t maximum_depth;
    uint32_t maximum_bucket_population;
} lzrc_sg_statistics;

typedef struct lzrc_sg_dictionary lzrc_sg_dictionary;

uint32_t lzrc_sg_depth_limit(uint32_t maximum_size);

lzrc_sg_dictionary *lzrc_sg_dictionary_create(const uint8_t *input,
                                               size_t input_size,
                                               uint32_t window_size,
                                               uint16_t maximum_match_length,
                                               uint32_t hash_count,
                                               uint32_t near_limit,
                                               uint32_t far_limit);
void lzrc_sg_dictionary_destroy(lzrc_sg_dictionary *dictionary);

lzrc_sg_match lzrc_sg_dictionary_query_far(
    lzrc_sg_dictionary *dictionary, size_t current_position);
lzrc_sg_match lzrc_sg_dictionary_query_huge(
    lzrc_sg_dictionary *dictionary, size_t current_position);
bool lzrc_sg_dictionary_far_uses_tree(
    const lzrc_sg_dictionary *dictionary, size_t current_position);
bool lzrc_sg_dictionary_huge_uses_tree(
    const lzrc_sg_dictionary *dictionary, size_t current_position);
bool lzrc_sg_dictionary_advance(lzrc_sg_dictionary *dictionary,
                                size_t position);

bool lzrc_sg_dictionary_validate(const lzrc_sg_dictionary *dictionary,
                                 size_t current_position);
lzrc_sg_statistics lzrc_sg_dictionary_statistics(
    const lzrc_sg_dictionary *dictionary);

#ifdef __cplusplus
}
#endif

#endif
