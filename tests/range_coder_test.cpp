#include "range_coder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::uint8_t> MakeBits(std::size_t count) {
    std::vector<std::uint8_t> bits;
    std::uint32_t state = 0xC001D00DU;
    bits.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        state = state * 1664525U + 1013904223U;
        bits.push_back(static_cast<std::uint8_t>(state >> 31U));
    }
    return bits;
}

std::vector<std::uint8_t> Encode16(const std::vector<std::uint8_t>& bits,
                                   bool use_table) {
    std::vector<std::uint8_t> output(bits.size() + 32U);
    lzrc_range16_encoder encoder;
    std::array<lzrc_context, 7> contexts{};
    lzrc_contexts_init(contexts.data(), contexts.size());
    lzrc_range16_encoder_init(&encoder, output.data(), output.size(), use_table);

    for (std::size_t i = 0; i < bits.size(); ++i) {
        EXPECT_TRUE(lzrc_range16_encode_bit(&encoder, &contexts[i % contexts.size()],
                                            bits[i]));
    }
    EXPECT_TRUE(lzrc_range16_encoder_finish(&encoder));
    output.resize(lzrc_range16_encoder_size(&encoder));
    return output;
}

std::vector<std::uint8_t> Encode32(const std::vector<std::uint8_t>& bits) {
    std::vector<std::uint8_t> output(bits.size() + 32U);
    lzrc_range32_encoder encoder;
    std::array<lzrc_context, 7> contexts{};
    lzrc_contexts_init(contexts.data(), contexts.size());
    lzrc_range32_encoder_init(&encoder, output.data(), output.size());

    for (std::size_t i = 0; i < bits.size(); ++i) {
        EXPECT_TRUE(lzrc_range32_encode_bit(&encoder, &contexts[i % contexts.size()],
                                            bits[i]));
    }
    EXPECT_TRUE(lzrc_range32_encoder_finish(&encoder));
    output.resize(lzrc_range32_encoder_size(&encoder));
    return output;
}

}  // namespace

TEST(ContextTest, InitializesAndUpdatesWithSpecifiedRule) {
    std::array<lzrc_context, 3> contexts{{{1U}, {2U}, {3U}}};
    lzrc_contexts_init(contexts.data(), contexts.size());
    for (const auto& context : contexts) {
        EXPECT_EQ(context.probability, 128U);
    }

    EXPECT_EQ(lzrc_probability_update(128U, 0U), 131U);
    EXPECT_EQ(lzrc_probability_update(128U, 1U), 124U);
    EXPECT_EQ(lzrc_probability_update(255U, 0U), 255U);
    EXPECT_EQ(lzrc_probability_update(0U, 1U), 0U);
}

TEST(Range16Test, RoundTripsAdaptiveRandomBits) {
    const auto bits = MakeBits(20000U);
    const auto encoded = Encode16(bits, false);
    lzrc_range16_decoder decoder;
    std::array<lzrc_context, 7> contexts{};
    lzrc_contexts_init(contexts.data(), contexts.size());
    ASSERT_TRUE(lzrc_range16_decoder_init(&decoder, encoded.data(), encoded.size(),
                                          false));

    for (std::size_t i = 0; i < bits.size(); ++i) {
        std::uint8_t decoded = 0U;
        ASSERT_TRUE(lzrc_range16_decode_bit(&decoder,
                                            &contexts[i % contexts.size()], &decoded));
        ASSERT_EQ(decoded, bits[i]) << "bit index=" << i;
    }
}

TEST(Range16Test, TableAndNativeModesProduceIdenticalBytes) {
    const auto bits = MakeBits(50000U);
    EXPECT_EQ(Encode16(bits, true), Encode16(bits, false));
}

TEST(Range16Test, EmptySequenceProducesDecoderInitializationData) {
    const std::vector<std::uint8_t> bits;
    const auto encoded = Encode16(bits, false);
    EXPECT_EQ(encoded.size(), 2U);

    lzrc_range16_decoder decoder;
    EXPECT_TRUE(lzrc_range16_decoder_init(&decoder, encoded.data(), encoded.size(),
                                          false));
}

TEST(Range16Test, ReportsOutputCapacityAndTruncatedInput) {
    std::array<std::uint8_t, 1> output{};
    lzrc_range16_encoder encoder;
    lzrc_range16_encoder_init(&encoder, output.data(), output.size(), false);
    EXPECT_FALSE(lzrc_range16_encoder_finish(&encoder));

    lzrc_range16_decoder decoder;
    EXPECT_FALSE(lzrc_range16_decoder_init(&decoder, output.data(), output.size(),
                                           false));
}

TEST(Range32Test, RoundTripsAdaptiveRandomBits) {
    const auto bits = MakeBits(20000U);
    const auto encoded = Encode32(bits);
    lzrc_range32_decoder decoder;
    std::array<lzrc_context, 7> contexts{};
    lzrc_contexts_init(contexts.data(), contexts.size());
    ASSERT_TRUE(lzrc_range32_decoder_init(&decoder, encoded.data(), encoded.size()));

    for (std::size_t i = 0; i < bits.size(); ++i) {
        std::uint8_t decoded = 0U;
        ASSERT_TRUE(lzrc_range32_decode_bit(&decoder,
                                            &contexts[i % contexts.size()], &decoded));
        ASSERT_EQ(decoded, bits[i]) << "bit index=" << i;
    }
}

TEST(Range32Test, EmptySequenceProducesDecoderInitializationData) {
    const std::vector<std::uint8_t> bits;
    const auto encoded = Encode32(bits);
    EXPECT_EQ(encoded.size(), 4U);

    lzrc_range32_decoder decoder;
    EXPECT_TRUE(lzrc_range32_decoder_init(&decoder, encoded.data(), encoded.size()));
}

TEST(Range32Test, ReportsOutputCapacityAndTruncatedInput) {
    std::array<std::uint8_t, 3> output{};
    lzrc_range32_encoder encoder;
    lzrc_range32_encoder_init(&encoder, output.data(), output.size());
    EXPECT_FALSE(lzrc_range32_encoder_finish(&encoder));

    lzrc_range32_decoder decoder;
    EXPECT_FALSE(lzrc_range32_decoder_init(&decoder, output.data(), output.size()));
}
