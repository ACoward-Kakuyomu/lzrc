#ifndef LZRC_MATH_H
#define LZRC_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t lzrc_mul8x8(uint8_t x, uint8_t y);
uint16_t lzrc_bound16_table(uint16_t range, uint8_t probability);
uint16_t lzrc_bound16_native(uint16_t range, uint8_t probability);
uint32_t lzrc_bound32_native(uint32_t range, uint8_t probability);

#ifdef __cplusplus
}
#endif

#endif
