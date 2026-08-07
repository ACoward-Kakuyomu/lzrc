#include "lzrc/lzrc_math.h"

#include <cstdint>

#include <gtest/gtest.h>

TEST(Mul8x8Test, MatchesNativeMultiplicationForEveryInput) {
    for (std::uint32_t x = 0; x <= UINT8_MAX; ++x) {
        for (std::uint32_t y = 0; y <= UINT8_MAX; ++y) {
            const auto expected = static_cast<std::uint16_t>(x * y);
            EXPECT_EQ(lzrc_mul8x8(static_cast<std::uint8_t>(x),
                                  static_cast<std::uint8_t>(y)),
                      expected)
                << "x=" << x << ", y=" << y;
        }
    }
}

TEST(Bound16Test, TableAndNativeImplementationsMatchForEveryInput) {
    for (std::uint32_t range = 0; range <= UINT16_MAX; ++range) {
        for (std::uint32_t probability = 0; probability <= UINT8_MAX;
             ++probability) {
            const auto expected = static_cast<std::uint16_t>(
                (range * probability) >> 8U);
            const auto range16 = static_cast<std::uint16_t>(range);
            const auto probability8 = static_cast<std::uint8_t>(probability);

            ASSERT_EQ(lzrc_bound16_table(range16, probability8), expected)
                << "range=" << range << ", probability=" << probability;
            ASSERT_EQ(lzrc_bound16_native(range16, probability8), expected)
                << "range=" << range << ", probability=" << probability;
        }
    }
}

TEST(Bound32Test, HandlesExactBoundaryValues) {
    EXPECT_EQ(lzrc_bound32_native(0U, 128U), 0U);
    EXPECT_EQ(lzrc_bound32_native(UINT32_MAX, 0U), 0U);
    EXPECT_EQ(lzrc_bound32_native(UINT32_MAX, 255U),
              static_cast<std::uint32_t>(
                  (static_cast<std::uint64_t>(UINT32_MAX) * 255U) >> 8U));
    EXPECT_EQ(lzrc_bound32_native(UINT32_MAX, 128U), 0x7FFFFFFFU);
}
