#include "scapegoat_matchfinder.h"

#include <stdlib.h>
#include <string.h>

#define LZRC_SG_NIL UINT32_MAX
#define LZRC_SG_MAX_PATH 64U
#define LZRC_SG_BUCKET_TREE_LIMIT 4096U

typedef uint32_t lzrc_sg_index;

typedef struct lzrc_sg_node {
    lzrc_sg_index left;
    lzrc_sg_index right;
} lzrc_sg_node;

typedef struct lzrc_sg_bucket {
    lzrc_sg_index root;
    uint32_t size;
    uint32_t max_size;
} lzrc_sg_bucket;

typedef struct lzrc_sg_forest {
    lzrc_sg_bucket *buckets;
    uint8_t *fallback_buckets;
    uint32_t hash_count;
    lzrc_sg_statistics statistics;
} lzrc_sg_forest;

struct lzrc_sg_dictionary {
    const uint8_t *input;
    size_t input_size;
    uint32_t window_size;
    uint32_t window_mask;
    uint32_t near_limit;
    uint32_t far_limit;
    uint16_t maximum_match_length;
    size_t node_capacity;
    lzrc_sg_node *nodes;
    lzrc_sg_forest far_forest;
    lzrc_sg_forest huge_forest;
    bool has_huge;
};

static const uint32_t kDepthThresholds[] = {
    1U,       2U,       3U,       4U,       6U,       8U,
    12U,      18U,      26U,      39U,      58U,      87U,
    130U,     195U,     292U,     438U,     657U,     986U,
    1478U,    2217U,    3326U,    4988U,    7482U,    11223U,
    16835U,   25252U,   37877U,   56816U,   85223U,   127835U,
    191752U,  287627U,  431440U,  647160U,  970740U,  1456110U,
    2184165U, 3276247U, 4914370U, 7371555U, 11057333U, 16585999U,
    24878998U};

uint32_t lzrc_sg_depth_limit(uint32_t maximum_size) {
    uint32_t depth = 0U;
    const uint32_t count =
        (uint32_t)(sizeof(kDepthThresholds) / sizeof(kDepthThresholds[0]));
    while (depth + 1U < count &&
           kDepthThresholds[depth + 1U] <= maximum_size) {
        ++depth;
    }
    return depth;
}

static bool is_power_of_two(uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

static uint32_t hash3(const lzrc_sg_dictionary *dictionary,
                      size_t position) {
    uint32_t hash = dictionary->input[position];
    hash = hash * UINT32_C(251) + dictionary->input[position + 1U];
    hash = hash * UINT32_C(251) + dictionary->input[position + 2U];
    return hash & (dictionary->far_forest.hash_count - UINT32_C(1));
}

static size_t position_of(const lzrc_sg_dictionary *dictionary,
                          lzrc_sg_index node,
                          size_t reference_position) {
    uint32_t age =
        ((uint32_t)reference_position - node) & dictionary->window_mask;
    if (age == 0U) {
        age = dictionary->window_size;
    }
    return reference_position - age;
}

static int compare_positions(lzrc_sg_dictionary *dictionary,
                             size_t first,
                             size_t second,
                             uint16_t *common_length,
                             uint64_t *comparison_count) {
    size_t limit = dictionary->maximum_match_length;
    size_t index = 0U;
    const size_t first_remaining = dictionary->input_size - first;
    const size_t second_remaining = dictionary->input_size - second;
    if (limit > first_remaining) {
        limit = first_remaining;
    }
    if (limit > second_remaining) {
        limit = second_remaining;
    }
    if (limit != 0U &&
        memcmp(dictionary->input + first, dictionary->input + second,
               limit) != 0) {
        while (dictionary->input[first + index] ==
               dictionary->input[second + index]) {
            ++index;
        }
        if (comparison_count != NULL) {
            *comparison_count += index + 1U;
        }
        *common_length = (uint16_t)index;
        return (dictionary->input[first + index] <
                dictionary->input[second + index])
                   ? -1
                   : 1;
    }
    index = limit;
    if (comparison_count != NULL) {
        *comparison_count += limit;
    }
    *common_length = (uint16_t)index;
    if (index == dictionary->maximum_match_length) {
        if (first < second) {
            return -1;
        }
        return (first > second) ? 1 : 0;
    }
    if (first_remaining < second_remaining) {
        return -1;
    }
    if (first_remaining > second_remaining) {
        return 1;
    }
    if (first < second) {
        return -1;
    }
    return (first > second) ? 1 : 0;
}

static uint32_t count_subtree(const lzrc_sg_dictionary *dictionary,
                              lzrc_sg_index root) {
    if (root == LZRC_SG_NIL) {
        return 0U;
    }
    return UINT32_C(1) + count_subtree(dictionary,
                                      dictionary->nodes[root].left) +
           count_subtree(dictionary, dictionary->nodes[root].right);
}

static void flatten_subtree(const lzrc_sg_dictionary *dictionary,
                            lzrc_sg_index root,
                            lzrc_sg_index *nodes,
                            uint32_t *next) {
    if (root == LZRC_SG_NIL) {
        return;
    }
    flatten_subtree(dictionary, dictionary->nodes[root].left, nodes, next);
    nodes[*next] = root;
    ++(*next);
    flatten_subtree(dictionary, dictionary->nodes[root].right, nodes, next);
}

static lzrc_sg_index build_balanced(lzrc_sg_dictionary *dictionary,
                                    const lzrc_sg_index *nodes,
                                    uint32_t begin,
                                    uint32_t end) {
    uint32_t middle;
    lzrc_sg_index root;
    if (begin == end) {
        return LZRC_SG_NIL;
    }
    middle = begin + (end - begin) / UINT32_C(2);
    root = nodes[middle];
    dictionary->nodes[root].left =
        build_balanced(dictionary, nodes, begin, middle);
    dictionary->nodes[root].right =
        build_balanced(dictionary, nodes, middle + UINT32_C(1), end);
    return root;
}

static bool rebuild_subtree(lzrc_sg_dictionary *dictionary,
                            lzrc_sg_forest *forest,
                            lzrc_sg_index *link,
                            uint32_t size,
                            bool full_rebuild) {
    lzrc_sg_index *ordered;
    uint32_t count = 0U;
    if (size == 0U) {
        *link = LZRC_SG_NIL;
        return true;
    }
    if ((size_t)size > SIZE_MAX / sizeof(*ordered)) {
        return false;
    }
    ordered = (lzrc_sg_index *)malloc((size_t)size * sizeof(*ordered));
    if (ordered == NULL) {
        return false;
    }
    flatten_subtree(dictionary, *link, ordered, &count);
    if (count != size) {
        free(ordered);
        return false;
    }
    *link = build_balanced(dictionary, ordered, 0U, size);
    free(ordered);
    if (full_rebuild) {
        ++forest->statistics.full_rebuild_count;
    } else {
        ++forest->statistics.subtree_rebuild_count;
    }
    forest->statistics.rebuilt_node_count += size;
    return true;
}

static bool forest_insert(lzrc_sg_dictionary *dictionary,
                          lzrc_sg_forest *forest,
                          size_t position,
                          size_t reference_position) {
    lzrc_sg_index path[LZRC_SG_MAX_PATH];
    uint32_t depth = 0U;
    const uint32_t hash = hash3(dictionary, position);
    lzrc_sg_bucket *bucket = &forest->buckets[hash];
    const lzrc_sg_index inserted =
        (lzrc_sg_index)(position & dictionary->window_mask);
    lzrc_sg_index *link = &bucket->root;
    uint16_t ignored_length;

    if (forest->fallback_buckets[hash] != 0U) {
        return true;
    }
    if (bucket->size >= LZRC_SG_BUCKET_TREE_LIMIT) {
        bucket->root = LZRC_SG_NIL;
        bucket->size = 0U;
        bucket->max_size = 0U;
        forest->fallback_buckets[hash] = 1U;
        ++forest->statistics.fallback_bucket_count;
        return true;
    }
    if ((size_t)inserted >= dictionary->node_capacity) {
        return false;
    }
    while (*link != LZRC_SG_NIL) {
        const lzrc_sg_index node = *link;
        const size_t node_position =
            position_of(dictionary, node, reference_position);
        int comparison;
        if (depth == LZRC_SG_MAX_PATH) {
            return false;
        }
        path[depth] = node;
        ++depth;
        comparison = compare_positions(
            dictionary, position, node_position, &ignored_length,
            &forest->statistics.candidate_byte_comparison_count);
        link = (comparison < 0) ? &dictionary->nodes[node].left
                                : &dictionary->nodes[node].right;
    }

    dictionary->nodes[inserted].left = LZRC_SG_NIL;
    dictionary->nodes[inserted].right = LZRC_SG_NIL;
    *link = inserted;
    ++bucket->size;
    if (bucket->size > bucket->max_size) {
        bucket->max_size = bucket->size;
    }
    ++forest->statistics.insert_count;
    if (bucket->size > forest->statistics.maximum_bucket_population) {
        forest->statistics.maximum_bucket_population = bucket->size;
    }
    if (depth > forest->statistics.maximum_depth) {
        forest->statistics.maximum_depth = depth;
    }

    if (depth > lzrc_sg_depth_limit(bucket->max_size)) {
        lzrc_sg_index child = inserted;
        uint32_t child_size = 1U;
        uint32_t path_index = depth;
        while (path_index != 0U) {
            const lzrc_sg_index parent = path[path_index - 1U];
            const lzrc_sg_index sibling =
                (dictionary->nodes[parent].left == child)
                    ? dictionary->nodes[parent].right
                    : dictionary->nodes[parent].left;
            const uint32_t parent_size =
                child_size + count_subtree(dictionary, sibling) + UINT32_C(1);
            if (UINT32_C(3) * child_size >
                UINT32_C(2) * parent_size) {
                lzrc_sg_index *parent_link;
                if (path_index == 1U) {
                    parent_link = &bucket->root;
                } else {
                    const lzrc_sg_index grandparent = path[path_index - 2U];
                    parent_link =
                        (dictionary->nodes[grandparent].left == parent)
                            ? &dictionary->nodes[grandparent].left
                            : &dictionary->nodes[grandparent].right;
                }
                return rebuild_subtree(dictionary, forest, parent_link,
                                       parent_size, false);
            }
            child = parent;
            child_size = parent_size;
            --path_index;
        }
    }
    return true;
}

static bool forest_delete(lzrc_sg_dictionary *dictionary,
                          lzrc_sg_forest *forest,
                          size_t position,
                          size_t reference_position) {
    const uint32_t hash = hash3(dictionary, position);
    lzrc_sg_bucket *bucket = &forest->buckets[hash];
    lzrc_sg_index *target_link = &bucket->root;
    lzrc_sg_index target;
    uint16_t ignored_length;

    if (forest->fallback_buckets[hash] != 0U) {
        return true;
    }
    while (*target_link != LZRC_SG_NIL) {
        const lzrc_sg_index node = *target_link;
        const size_t node_position =
            position_of(dictionary, node, reference_position);
        const int comparison = compare_positions(
            dictionary, position, node_position, &ignored_length,
            &forest->statistics.candidate_byte_comparison_count);
        if (comparison == 0) {
            break;
        }
        target_link = (comparison < 0) ? &dictionary->nodes[node].left
                                       : &dictionary->nodes[node].right;
    }
    if (*target_link == LZRC_SG_NIL) {
        return false;
    }
    target = *target_link;
    if (dictionary->nodes[target].left == LZRC_SG_NIL) {
        *target_link = dictionary->nodes[target].right;
    } else if (dictionary->nodes[target].right == LZRC_SG_NIL) {
        *target_link = dictionary->nodes[target].left;
    } else {
        lzrc_sg_index *successor_link = &dictionary->nodes[target].right;
        lzrc_sg_index successor;
        while (dictionary->nodes[*successor_link].left != LZRC_SG_NIL) {
            successor_link = &dictionary->nodes[*successor_link].left;
        }
        successor = *successor_link;
        if (successor != dictionary->nodes[target].right) {
            *successor_link = dictionary->nodes[successor].right;
            dictionary->nodes[successor].right = dictionary->nodes[target].right;
        }
        dictionary->nodes[successor].left = dictionary->nodes[target].left;
        *target_link = successor;
    }
    dictionary->nodes[target].left = LZRC_SG_NIL;
    dictionary->nodes[target].right = LZRC_SG_NIL;
    --bucket->size;
    ++forest->statistics.delete_count;

    if (bucket->size == 0U) {
        bucket->root = LZRC_SG_NIL;
        bucket->max_size = 0U;
    } else if (UINT32_C(3) * bucket->size <
               UINT32_C(2) * bucket->max_size) {
        if (!rebuild_subtree(dictionary, forest, &bucket->root, bucket->size,
                             true)) {
            return false;
        }
        bucket->max_size = bucket->size;
    }
    return true;
}

static lzrc_sg_match forest_query(lzrc_sg_dictionary *dictionary,
                                  lzrc_sg_forest *forest,
                                  size_t current_position) {
    lzrc_sg_match best = {0U, 0U};
    lzrc_sg_index node;
    if (dictionary == NULL || current_position >= dictionary->input_size ||
        dictionary->input_size - current_position < 3U) {
        return best;
    }
    node = forest->buckets[hash3(dictionary, current_position)].root;
    while (node != LZRC_SG_NIL) {
        const size_t candidate_position =
            position_of(dictionary, node, current_position);
        const uint32_t offset =
            (uint32_t)(current_position - candidate_position);
        uint16_t common_length;
        const int comparison = compare_positions(
            dictionary, current_position, candidate_position, &common_length,
            &forest->statistics.candidate_byte_comparison_count);
        ++forest->statistics.visited_node_count;
        if (common_length > best.length ||
            (common_length == best.length && common_length >= 3U &&
             offset < best.offset)) {
            best.length = common_length;
            best.offset = offset;
        }
        if (common_length == dictionary->maximum_match_length) {
            break;
        }
        node = (comparison < 0) ? dictionary->nodes[node].left
                                : dictionary->nodes[node].right;
    }
    return best;
}

static bool forest_init(lzrc_sg_forest *forest, uint32_t hash_count) {
    size_t index;
    if ((size_t)hash_count > SIZE_MAX / sizeof(*forest->buckets)) {
        return false;
    }
    forest->buckets =
        (lzrc_sg_bucket *)malloc((size_t)hash_count * sizeof(*forest->buckets));
    forest->fallback_buckets =
        (uint8_t *)calloc((size_t)hash_count, sizeof(*forest->fallback_buckets));
    if (forest->buckets == NULL || forest->fallback_buckets == NULL) {
        free(forest->fallback_buckets);
        free(forest->buckets);
        forest->fallback_buckets = NULL;
        forest->buckets = NULL;
        return false;
    }
    forest->hash_count = hash_count;
    memset(&forest->statistics, 0, sizeof(forest->statistics));
    for (index = 0U; index < hash_count; ++index) {
        forest->buckets[index].root = LZRC_SG_NIL;
        forest->buckets[index].size = 0U;
        forest->buckets[index].max_size = 0U;
    }
    return true;
}

lzrc_sg_dictionary *lzrc_sg_dictionary_create(const uint8_t *input,
                                               size_t input_size,
                                               uint32_t window_size,
                                               uint16_t maximum_match_length,
                                               uint32_t hash_count,
                                               uint32_t near_limit,
                                               uint32_t far_limit) {
    lzrc_sg_dictionary *dictionary;
    size_t node_capacity;
    if ((input == NULL && input_size != 0U) || !is_power_of_two(window_size) ||
        !is_power_of_two(hash_count) || maximum_match_length == 0U ||
        near_limit >= far_limit || far_limit > window_size) {
        return NULL;
    }
    node_capacity = input_size;
    if (node_capacity > window_size) {
        node_capacity = window_size;
    }
    if (node_capacity > SIZE_MAX / sizeof(lzrc_sg_node)) {
        return NULL;
    }
    dictionary = (lzrc_sg_dictionary *)calloc(1U, sizeof(*dictionary));
    if (dictionary == NULL) {
        return NULL;
    }
    dictionary->nodes =
        (lzrc_sg_node *)malloc(node_capacity * sizeof(*dictionary->nodes));
    if (dictionary->nodes == NULL && node_capacity != 0U) {
        free(dictionary);
        return NULL;
    }
    dictionary->input = input;
    dictionary->input_size = input_size;
    dictionary->window_size = window_size;
    dictionary->window_mask = window_size - UINT32_C(1);
    dictionary->near_limit = near_limit;
    dictionary->far_limit = far_limit;
    dictionary->maximum_match_length = maximum_match_length;
    dictionary->node_capacity = node_capacity;
    dictionary->has_huge = far_limit < window_size;
    if (!forest_init(&dictionary->far_forest, hash_count) ||
        (dictionary->has_huge &&
         !forest_init(&dictionary->huge_forest, hash_count))) {
        lzrc_sg_dictionary_destroy(dictionary);
        return NULL;
    }
    return dictionary;
}

void lzrc_sg_dictionary_destroy(lzrc_sg_dictionary *dictionary) {
    if (dictionary != NULL) {
        free(dictionary->huge_forest.fallback_buckets);
        free(dictionary->huge_forest.buckets);
        free(dictionary->far_forest.fallback_buckets);
        free(dictionary->far_forest.buckets);
        free(dictionary->nodes);
        free(dictionary);
    }
}

lzrc_sg_match lzrc_sg_dictionary_query_far(
    lzrc_sg_dictionary *dictionary, size_t current_position) {
    lzrc_sg_match empty = {0U, 0U};
    if (dictionary == NULL) {
        return empty;
    }
    return forest_query(dictionary, &dictionary->far_forest, current_position);
}

lzrc_sg_match lzrc_sg_dictionary_query_huge(
    lzrc_sg_dictionary *dictionary, size_t current_position) {
    lzrc_sg_match empty = {0U, 0U};
    if (dictionary == NULL || !dictionary->has_huge) {
        return empty;
    }
    return forest_query(dictionary, &dictionary->huge_forest,
                        current_position);
}

bool lzrc_sg_dictionary_far_uses_tree(
    const lzrc_sg_dictionary *dictionary, size_t current_position) {
    if (dictionary == NULL || current_position >= dictionary->input_size ||
        dictionary->input_size - current_position < 3U) {
        return true;
    }
    return dictionary->far_forest.fallback_buckets[
               hash3(dictionary, current_position)] == 0U;
}

bool lzrc_sg_dictionary_huge_uses_tree(
    const lzrc_sg_dictionary *dictionary, size_t current_position) {
    if (dictionary == NULL || !dictionary->has_huge ||
        current_position >= dictionary->input_size ||
        dictionary->input_size - current_position < 3U) {
        return true;
    }
    return dictionary->huge_forest.fallback_buckets[
               hash3(dictionary, current_position)] == 0U;
}

bool lzrc_sg_dictionary_advance(lzrc_sg_dictionary *dictionary,
                                size_t position) {
    if (dictionary == NULL || position >= dictionary->input_size) {
        return false;
    }
    if (dictionary->has_huge && position >= dictionary->window_size &&
        !forest_delete(dictionary, &dictionary->huge_forest,
                       position - dictionary->window_size, position)) {
        return false;
    }
    if (position >= dictionary->far_limit) {
        const size_t moved = position - dictionary->far_limit;
        if (!forest_delete(dictionary, &dictionary->far_forest, moved,
                           position)) {
            return false;
        }
        if (dictionary->has_huge &&
            !forest_insert(dictionary, &dictionary->huge_forest, moved,
                           position)) {
            return false;
        }
    }
    if (position >= dictionary->near_limit &&
        !forest_insert(dictionary, &dictionary->far_forest,
                       position - dictionary->near_limit, position)) {
        return false;
    }
    return true;
}

typedef struct lzrc_sg_validation {
    const lzrc_sg_dictionary *dictionary;
    size_t reference_position;
    uint32_t minimum_age;
    uint32_t maximum_age;
    uint8_t *visited;
    bool has_previous;
    size_t previous_position;
    uint32_t count;
} lzrc_sg_validation;

static bool validate_subtree(lzrc_sg_validation *validation,
                             lzrc_sg_index root,
                             uint32_t expected_hash) {
    const lzrc_sg_dictionary *dictionary = validation->dictionary;
    size_t position;
    uint32_t age;
    uint16_t ignored_length;
    if (root == LZRC_SG_NIL) {
        return true;
    }
    if ((size_t)root >= dictionary->node_capacity ||
        validation->visited[root] != 0U) {
        return false;
    }
    validation->visited[root] = 1U;
    if (!validate_subtree(validation, dictionary->nodes[root].left,
                          expected_hash)) {
        return false;
    }
    age = ((uint32_t)validation->reference_position - root) &
          dictionary->window_mask;
    if (age == 0U) {
        age = dictionary->window_size;
    }
    if (age < validation->minimum_age || age > validation->maximum_age ||
        validation->reference_position < age) {
        return false;
    }
    position = validation->reference_position - age;
    if (hash3(dictionary, position) != expected_hash) {
        return false;
    }
    if (validation->has_previous &&
        compare_positions((lzrc_sg_dictionary *)dictionary,
                          validation->previous_position, position,
                          &ignored_length, NULL) >= 0) {
        return false;
    }
    validation->has_previous = true;
    validation->previous_position = position;
    ++validation->count;
    return validate_subtree(validation, dictionary->nodes[root].right,
                            expected_hash);
}

static bool validate_forest(const lzrc_sg_dictionary *dictionary,
                            const lzrc_sg_forest *forest,
                            size_t current_position,
                            uint32_t minimum_age,
                            uint32_t maximum_age,
                            uint8_t *visited) {
    uint32_t hash;
    for (hash = 0U; hash < forest->hash_count; ++hash) {
        lzrc_sg_validation validation;
        validation.dictionary = dictionary;
        validation.reference_position = current_position;
        validation.minimum_age = minimum_age;
        validation.maximum_age = maximum_age;
        validation.visited = visited;
        validation.has_previous = false;
        validation.previous_position = 0U;
        validation.count = 0U;
        if (forest->fallback_buckets[hash] != 0U) {
            if (forest->buckets[hash].root != LZRC_SG_NIL ||
                forest->buckets[hash].size != 0U ||
                forest->buckets[hash].max_size != 0U) {
                return false;
            }
            continue;
        }
        if (!validate_subtree(&validation, forest->buckets[hash].root, hash) ||
            validation.count != forest->buckets[hash].size ||
            forest->buckets[hash].size > forest->buckets[hash].max_size) {
            return false;
        }
    }
    return true;
}

bool lzrc_sg_dictionary_validate(const lzrc_sg_dictionary *dictionary,
                                 size_t current_position) {
    uint8_t *visited;
    bool valid;
    if (dictionary == NULL || current_position > dictionary->input_size) {
        return false;
    }
    visited = (uint8_t *)calloc(dictionary->node_capacity, sizeof(*visited));
    if (visited == NULL && dictionary->node_capacity != 0U) {
        return false;
    }
    valid = validate_forest(dictionary, &dictionary->far_forest,
                            current_position, dictionary->near_limit + 1U,
                            dictionary->far_limit, visited);
    if (valid && dictionary->has_huge) {
        valid = validate_forest(dictionary, &dictionary->huge_forest,
                                current_position, dictionary->far_limit + 1U,
                                dictionary->window_size, visited);
    }
    free(visited);
    return valid;
}

static void add_statistics(lzrc_sg_statistics *total,
                           const lzrc_sg_statistics *part) {
    total->candidate_byte_comparison_count +=
        part->candidate_byte_comparison_count;
    total->visited_node_count += part->visited_node_count;
    total->insert_count += part->insert_count;
    total->delete_count += part->delete_count;
    total->subtree_rebuild_count += part->subtree_rebuild_count;
    total->full_rebuild_count += part->full_rebuild_count;
    total->rebuilt_node_count += part->rebuilt_node_count;
    total->fallback_bucket_count += part->fallback_bucket_count;
    if (part->maximum_depth > total->maximum_depth) {
        total->maximum_depth = part->maximum_depth;
    }
    if (part->maximum_bucket_population > total->maximum_bucket_population) {
        total->maximum_bucket_population = part->maximum_bucket_population;
    }
}

lzrc_sg_statistics lzrc_sg_dictionary_statistics(
    const lzrc_sg_dictionary *dictionary) {
    lzrc_sg_statistics statistics;
    memset(&statistics, 0, sizeof(statistics));
    if (dictionary != NULL) {
        add_statistics(&statistics, &dictionary->far_forest.statistics);
        if (dictionary->has_huge) {
            add_statistics(&statistics, &dictionary->huge_forest.statistics);
        }
    }
    return statistics;
}
