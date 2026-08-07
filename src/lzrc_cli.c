#include "lzrc/lzrc.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_file(const char *path, uint8_t **data, size_t *size) {
    FILE *file;
    uint8_t *buffer = NULL;
    size_t capacity = 0U;
    size_t used = 0U;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "lzrc: cannot open '%s': %s\n", path, strerror(errno));
        return false;
    }
    for (;;) {
        size_t read_size;
        if (used == capacity) {
            size_t new_capacity = (capacity == 0U) ? 4096U : capacity * 2U;
            uint8_t *new_buffer;
            if (new_capacity < capacity || new_capacity > UINT32_MAX) {
                new_capacity = UINT32_MAX;
            }
            if (new_capacity == capacity) {
                fprintf(stderr, "lzrc: input exceeds UINT32_MAX bytes\n");
                free(buffer);
                fclose(file);
                return false;
            }
            new_buffer = (uint8_t *)realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                fprintf(stderr, "lzrc: out of memory while reading '%s'\n", path);
                free(buffer);
                fclose(file);
                return false;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }
        read_size = fread(buffer + used, 1U, capacity - used, file);
        used += read_size;
        if (read_size == 0U) {
            if (ferror(file)) {
                fprintf(stderr, "lzrc: cannot read '%s'\n", path);
                free(buffer);
                fclose(file);
                return false;
            }
            break;
        }
    }
    fclose(file);
    *data = buffer;
    *size = used;
    return true;
}

static bool write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "lzrc: cannot create '%s': %s\n", path,
                strerror(errno));
        return false;
    }
    if (size != 0U && fwrite(data, 1U, size, file) != size) {
        fprintf(stderr, "lzrc: cannot write '%s'\n", path);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "lzrc: cannot close '%s'\n", path);
        return false;
    }
    return true;
}

static int compress_file(unsigned int profile,
                         const char *input_path,
                         const char *output_path) {
    uint8_t *input = NULL;
    uint8_t *container = NULL;
    size_t input_size = 0U;
    size_t payload_bound;
    size_t payload_size = 0U;
    lzrc_result result;
    bool write_ok;

    if (!read_file(input_path, &input, &input_size)) {
        return 1;
    }
    result = lzrc_compress_bound(input_size, &payload_bound);
    if (result != LZRC_OK || payload_bound > SIZE_MAX - 5U) {
        fprintf(stderr, "lzrc: input is too large\n");
        free(input);
        return 1;
    }
    container = (uint8_t *)malloc(payload_bound + 5U);
    if (container == NULL) {
        fprintf(stderr, "lzrc: out of memory\n");
        free(input);
        return 1;
    }
    container[0] = (uint8_t)profile;
    container[1] = (uint8_t)(input_size & 0xFFU);
    container[2] = (uint8_t)((input_size >> 8U) & 0xFFU);
    container[3] = (uint8_t)((input_size >> 16U) & 0xFFU);
    container[4] = (uint8_t)((input_size >> 24U) & 0xFFU);
    result = lzrc_compress(profile, input, input_size, container + 5U,
                           payload_bound, &payload_size);
    if (result != LZRC_OK) {
        fprintf(stderr, "lzrc: compression failed: %s\n",
                lzrc_result_string(result));
        free(container);
        free(input);
        return 1;
    }
    write_ok = write_file(output_path, container, payload_size + 5U);
    free(container);
    free(input);
    return write_ok ? 0 : 1;
}

static int decompress_file(const char *input_path, const char *output_path) {
    uint8_t *container = NULL;
    uint8_t *output = NULL;
    size_t container_size = 0U;
    size_t output_size = 0U;
    uint32_t original_size;
    unsigned int profile;
    lzrc_result result;
    bool write_ok;

    if (!read_file(input_path, &container, &container_size)) {
        return 1;
    }
    if (container_size < 5U) {
        fprintf(stderr, "lzrc: truncated container header\n");
        free(container);
        return 1;
    }
    profile = container[0];
    original_size = (uint32_t)container[1] |
                    ((uint32_t)container[2] << 8U) |
                    ((uint32_t)container[3] << 16U) |
                    ((uint32_t)container[4] << 24U);
    if (original_size != 0U) {
        output = (uint8_t *)malloc(original_size);
        if (output == NULL) {
            fprintf(stderr, "lzrc: out of memory\n");
            free(container);
            return 1;
        }
    }
    result = lzrc_decompress(profile, container + 5U, container_size - 5U,
                             original_size, output, original_size, &output_size);
    if (result != LZRC_OK) {
        fprintf(stderr, "lzrc: decompression failed: %s\n",
                lzrc_result_string(result));
        free(output);
        free(container);
        return 1;
    }
    write_ok = write_file(output_path, output, output_size);
    free(output);
    free(container);
    return write_ok ? 0 : 1;
}

static void print_usage(void) {
    fprintf(stderr, "usage: lzrc c[0-4] input output\n"
                    "       lzrc d input output\n");
}

int main(int argc, char **argv) {
    if (argc != 4) {
        print_usage();
        return 2;
    }
    if (argv[1][0] == 'c' && argv[1][1] >= '0' && argv[1][1] <= '4' &&
        argv[1][2] == '\0') {
        return compress_file((unsigned int)(argv[1][1] - '0'), argv[2], argv[3]);
    }
    if (strcmp(argv[1], "d") == 0) {
        return decompress_file(argv[2], argv[3]);
    }
    print_usage();
    return 2;
}
