#include "bwt.h"

#include <stdlib.h>
#include <string.h>

lzrc_result lzrc_bwt_forward(const uint8_t *input, size_t size,
                            uint8_t *last, uint32_t *primary) {
    uint32_t *storage, *order, *classes, *shifted, *next, *counts;
    uint32_t n, i, width, class_count;
    size_t count_size;
    if (input == NULL || last == NULL || primary == NULL || size == 0U ||
        size > 8U * LZRC_BWT_BLOCK_UNIT) {
        return LZRC_ERROR_INVALID_ARGUMENT;
    }
    n = (uint32_t)size;
    count_size = size < 256U ? 256U : size;
    storage = (uint32_t *)malloc((4U * size + count_size) * sizeof(uint32_t));
    if (storage == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    order = storage;
    classes = order + size;
    shifted = classes + size;
    next = shifted + size;
    counts = next + size;
    memset(counts, 0, count_size * sizeof(uint32_t));
    for (i = 0U; i < n; ++i) ++counts[input[i]];
    for (i = 1U; i < 256U; ++i) counts[i] += counts[i - 1U];
    for (i = n; i > 0U; --i) order[--counts[input[i - 1U]]] = i - 1U;
    class_count = 1U;
    classes[order[0]] = 0U;
    for (i = 1U; i < n; ++i) {
        if (input[order[i]] != input[order[i - 1U]]) ++class_count;
        classes[order[i]] = class_count - 1U;
    }
    /* Stable radix sorting of pairs of cyclic ranks; no rotation strings. */
    for (width = 1U; width < n && class_count < n; width <<= 1U) {
        uint32_t new_count = 1U;
        uint32_t *swap;
        for (i = 0U; i < n; ++i)
            shifted[i] = (order[i] + n - width) % n;
        memset(counts, 0, (size_t)class_count * sizeof(uint32_t));
        for (i = 0U; i < n; ++i) ++counts[classes[shifted[i]]];
        for (i = 1U; i < class_count; ++i) counts[i] += counts[i - 1U];
        for (i = n; i > 0U; --i) {
            const uint32_t p = shifted[i - 1U];
            order[--counts[classes[p]]] = p;
        }
        next[order[0]] = 0U;
        for (i = 1U; i < n; ++i) {
            const uint32_t a = order[i], b = order[i - 1U];
            if (classes[a] != classes[b] ||
                classes[(a + width) % n] != classes[(b + width) % n])
                ++new_count;
            next[a] = new_count - 1U;
        }
        swap = classes; classes = next; next = swap;
        class_count = new_count;
    }
    for (i = 0U; i < n; ++i) {
        last[i] = input[(order[i] + n - 1U) % n];
        if (order[i] == 0U) *primary = i;
    }
    free(storage);
    return LZRC_OK;
}

lzrc_result lzrc_bwt_inverse(const uint8_t *last, size_t size,
                            uint32_t primary, uint8_t *output) {
    uint32_t counts[256] = {0U};
    uint32_t starts[256], sum = 0U, row = primary;
    uint32_t *lf;
    size_t i;
    if (last == NULL || output == NULL || size == 0U ||
        size > 8U * LZRC_BWT_BLOCK_UNIT || primary >= size)
        return LZRC_ERROR_INVALID_ARGUMENT;
    lf = (uint32_t *)malloc(size * sizeof(uint32_t));
    if (lf == NULL) return LZRC_ERROR_OUT_OF_MEMORY;
    for (i = 0U; i < size; ++i) ++counts[last[i]];
    for (i = 0U; i < 256U; ++i) {
        starts[i] = sum;
        sum += counts[i];
    }
    for (i = 0U; i < size; ++i) lf[i] = starts[last[i]]++;
    for (i = size; i > 0U; --i) {
        output[i - 1U] = last[row];
        row = lf[row];
    }
    free(lf);
    return LZRC_OK;
}
