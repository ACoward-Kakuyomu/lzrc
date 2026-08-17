#include "carc.h"
#include "lzss_matchfinder_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct benchmark_result {
    double seconds;
    size_t output_size;
    size_t token_count;
    lzrc_lzss_matchfinder_statistics statistics;
} benchmark_result;

static bool encode_tokens(unsigned int profile,
                          const lzrc_token *tokens,
                          size_t token_count,
                          size_t input_size,
                          size_t *output_size) {
    const size_t context_count = lzrc_carc_context_count(profile);
    size_t output_capacity;
    uint8_t *output;
    lzrc_context *contexts;
    lzrc_carc_encoder encoder;
    size_t index;
    bool result = false;
    if (input_size > (SIZE_MAX - 64U) / 2U) {
        return false;
    }
    output_capacity = input_size * 2U + 64U;
    output = (uint8_t *)malloc(output_capacity);
    contexts = (lzrc_context *)malloc(context_count * sizeof(*contexts));
    if (output == NULL || contexts == NULL ||
        !lzrc_carc_encoder_init(&encoder, profile, output, output_capacity,
                                contexts, context_count)) {
        goto cleanup;
    }
    for (index = 0U; index < token_count; ++index) {
        if (!lzrc_carc_encode_token(&encoder, &tokens[index])) {
            goto cleanup;
        }
    }
    if (!lzrc_carc_encoder_finish(&encoder)) {
        goto cleanup;
    }
    *output_size = lzrc_carc_encoder_size(&encoder);
    result = true;

cleanup:
    free(contexts);
    free(output);
    return result;
}

static bool verify_tokens(unsigned int profile,
                          const uint8_t *input,
                          size_t input_size,
                          const lzrc_token *tokens,
                          size_t token_count) {
    uint8_t *decoded = (uint8_t *)malloc(input_size);
    size_t decoded_size = 0U;
    bool valid;
    if (decoded == NULL && input_size != 0U) {
        return false;
    }
    valid = lzrc_lzss_detokenize(profile, tokens, token_count, decoded,
                                 input_size, &decoded_size) &&
            decoded_size == input_size &&
            (input_size == 0U || memcmp(decoded, input, input_size) == 0);
    free(decoded);
    return valid;
}

static bool run_mode(unsigned int profile,
                     const uint8_t *input,
                     size_t input_size,
                     bool hybrid,
                     benchmark_result *result) {
    lzrc_token *tokens;
    size_t token_count = 0U;
    clock_t begin;
    clock_t end;
    bool valid;
    if (input_size > SIZE_MAX / sizeof(*tokens)) {
        return false;
    }
    tokens = (lzrc_token *)malloc(input_size * sizeof(*tokens));
    if (tokens == NULL && input_size != 0U) {
        return false;
    }
    begin = clock();
    valid = lzrc_lzss_tokenize_instrumented(
        profile, input, input_size, tokens, input_size, &token_count, hybrid,
        &result->statistics);
    end = clock();
    result->seconds = (double)(end - begin) / (double)CLOCKS_PER_SEC;
    result->token_count = token_count;
    if (valid) {
        valid = verify_tokens(profile, input, input_size, tokens, token_count) &&
                encode_tokens(profile, tokens, token_count, input_size,
                              &result->output_size);
    }
    free(tokens);
    return valid;
}

static void print_result(const char *name,
                         size_t input_size,
                         const benchmark_result *result) {
    const double mebibytes = (double)input_size / (1024.0 * 1024.0);
    const double throughput =
        (result->seconds > 0.0) ? mebibytes / result->seconds : 0.0;
    const lzrc_sg_statistics *sg = &result->statistics.binarytree;
    printf("%s\n", name);
    printf("  seconds: %.6f\n", result->seconds);
    printf("  MiB/s: %.3f\n", throughput);
    printf("  output bytes: %zu\n", result->output_size);
    printf("  token count: %zu\n", result->token_count);
    printf("  hash visited nodes: %llu\n",
           (unsigned long long)result->statistics.hash_visited_node_count);
    printf("  hash compared bytes: %llu\n",
           (unsigned long long)
               result->statistics.hash_candidate_byte_comparison_count);
    printf("  tree visited nodes: %llu\n",
           (unsigned long long)sg->visited_node_count);
    printf("  tree compared bytes: %llu\n",
           (unsigned long long)sg->candidate_byte_comparison_count);
    printf("  inserts/deletes: %llu/%llu\n",
           (unsigned long long)sg->insert_count,
           (unsigned long long)sg->delete_count);
    printf("  subtree/full rebuilds: %llu/%llu\n",
           (unsigned long long)sg->subtree_rebuild_count,
           (unsigned long long)sg->full_rebuild_count);
    printf("  rebuilt nodes: %llu\n",
           (unsigned long long)sg->rebuilt_node_count);
    printf("  maximum depth/bucket: %u/%u\n", sg->maximum_depth,
           sg->maximum_bucket_population);
    printf("  fallback buckets: %llu\n",
           (unsigned long long)sg->fallback_bucket_count);
}

int main(int argc, char **argv) {
    unsigned long parsed_profile;
    unsigned int profile;
    FILE *file;
    long file_length;
    size_t input_size;
    uint8_t *input;
    benchmark_result hash_result;
    benchmark_result hybrid_result;
    if (argc != 3) {
        fprintf(stderr, "usage: %s <profile 3|4> <input-file>\n", argv[0]);
        return 2;
    }
    errno = 0;
    parsed_profile = strtoul(argv[1], NULL, 10);
    if (errno != 0 || (parsed_profile != 3UL && parsed_profile != 4UL)) {
        fprintf(stderr, "profile must be 3 or 4\n");
        return 2;
    }
    profile = (unsigned int)parsed_profile;
    file = fopen(argv[2], "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (file_length = ftell(file)) < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "cannot inspect input file: %s\n", argv[2]);
        if (file != NULL) {
            fclose(file);
        }
        return 1;
    }
    input_size = (size_t)file_length;
    input = (uint8_t *)malloc(input_size);
    if ((input == NULL && input_size != 0U) ||
        fread(input, 1U, input_size, file) != input_size) {
        fprintf(stderr, "cannot read input file: %s\n", argv[2]);
        free(input);
        fclose(file);
        return 1;
    }
    fclose(file);
    memset(&hash_result, 0, sizeof(hash_result));
    memset(&hybrid_result, 0, sizeof(hybrid_result));
    if (!run_mode(profile, input, input_size, false, &hash_result) ||
        !run_mode(profile, input, input_size, true, &hybrid_result)) {
        fprintf(stderr, "benchmark or token verification failed\n");
        free(input);
        return 1;
    }
    printf("profile: %u\ninput bytes: %zu\n", profile, input_size);
    print_result("hash-chain", input_size, &hash_result);
    print_result("hybrid-binarytree", input_size, &hybrid_result);
    free(input);
    return 0;
}
