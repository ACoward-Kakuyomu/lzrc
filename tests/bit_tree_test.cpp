#include "bit_tree.h"
#include "range_coder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

void RoundTripEveryValue(unsigned int profile, unsigned int bit_width) {
    const std::uint32_t value_count = UINT32_C(1) << bit_width;
    std::vector<std::uint8_t> encoded(value_count * bit_width + 32U);
    std::vector<lzrc_context> encode_contexts(value_count);
    lzrc_range_encoder encoder;
    lzrc_contexts_init(encode_contexts.data(), encode_contexts.size());
    ASSERT_TRUE(lzrc_range_encoder_init(&encoder, profile, encoded.data(),
                                        encoded.size()));

    for (std::uint32_t value = 0U; value < value_count; ++value) {
        ASSERT_TRUE(lzrc_bit_tree_encode(&encoder, encode_contexts.data(),
                                         bit_width, value));
    }
    ASSERT_TRUE(lzrc_range_encoder_finish(&encoder));
    encoded.resize(lzrc_range_encoder_size(&encoder));

    std::vector<lzrc_context> decode_contexts(value_count);
    lzrc_range_decoder decoder;
    lzrc_contexts_init(decode_contexts.data(), decode_contexts.size());
    ASSERT_TRUE(lzrc_range_decoder_init(&decoder, profile, encoded.data(),
                                        encoded.size()));
    for (std::uint32_t expected = 0U; expected < value_count; ++expected) {
        std::uint32_t decoded = 0U;
        ASSERT_TRUE(lzrc_bit_tree_decode(&decoder, decode_contexts.data(),
                                         bit_width, &decoded));
        ASSERT_EQ(decoded, expected) << "bit width=" << bit_width;
    }
}

}  // namespace

TEST(BitTreeTest, RoundTripsAllValuesWith16BitNativeCore) {
    for (unsigned int bit_width = 1U; bit_width <= 8U; ++bit_width) {
        RoundTripEveryValue(1U, bit_width);
    }
}

TEST(BitTreeTest, RoundTripsAllValuesWith16BitTableCore) {
    for (unsigned int bit_width = 1U; bit_width <= 8U; ++bit_width) {
        RoundTripEveryValue(0U, bit_width);
    }
}

TEST(BitTreeTest, RoundTripsAllValuesWith32BitCore) {
    for (unsigned int bit_width = 1U; bit_width <= 8U; ++bit_width) {
        RoundTripEveryValue(4U, bit_width);
    }
}

TEST(BitTreeTest, RejectsInvalidWidthsAndValues) {
    std::array<std::uint8_t, 32> output{};
    std::array<lzrc_context, 256> contexts{};
    lzrc_range_encoder encoder;
    lzrc_contexts_init(contexts.data(), contexts.size());
    ASSERT_TRUE(lzrc_range_encoder_init(&encoder, 1U, output.data(), output.size()));

    EXPECT_FALSE(lzrc_bit_tree_encode(&encoder, contexts.data(), 0U, 0U));

    lzrc_range_encoder second_encoder;
    ASSERT_TRUE(lzrc_range_encoder_init(&second_encoder, 1U, output.data(),
                                        output.size()));
    EXPECT_FALSE(lzrc_bit_tree_encode(&second_encoder, contexts.data(), 8U,
                                      256U));
}

TEST(RangeFacadeTest, RejectsUnknownProfile) {
    std::array<std::uint8_t, 32> buffer{};
    lzrc_range_encoder encoder;
    lzrc_range_decoder decoder;
    EXPECT_FALSE(lzrc_range_encoder_init(&encoder, 5U, buffer.data(),
                                         buffer.size()));
    EXPECT_FALSE(lzrc_range_decoder_init(&decoder, 5U, buffer.data(),
                                         buffer.size()));
}
