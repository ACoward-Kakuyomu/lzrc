#include "carc.h"
#include "bit_tree.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<lzrc_token> TokensForProfile(unsigned int profile) {
    std::vector<lzrc_token> tokens{
        lzrc_token_literal(0x00U),
        lzrc_token_literal(0xFFU),
        lzrc_token_match(3U, 1U),
        lzrc_token_match(18U, 256U),
    };
    if (profile >= 1U) {
        tokens.push_back(lzrc_token_match(19U, 257U));
        tokens.push_back(lzrc_token_match(274U, 4096U));
    }
    if (profile >= 2U) {
        tokens.push_back(lzrc_token_match(19U, 4097U));
        tokens.push_back(lzrc_token_match(274U, 65536U));
    }
    if (profile >= 3U) {
        tokens.push_back(lzrc_token_match(19U, 65537U));
        tokens.push_back(lzrc_token_match(274U, 1048576U));
    }
    if (profile >= 4U) {
        tokens.push_back(lzrc_token_match(275U, 1048577U));
        tokens.push_back(lzrc_token_match(530U, 16777216U));
    }
    return tokens;
}

void RoundTripTokens(unsigned int profile) {
    const auto tokens = TokensForProfile(profile);
    std::vector<std::uint8_t> encoded(4096U);
    std::vector<lzrc_context> encode_contexts(lzrc_carc_context_count(profile));
    lzrc_carc_encoder encoder;
    ASSERT_TRUE(lzrc_carc_encoder_init(&encoder, profile, encoded.data(),
                                       encoded.size(), encode_contexts.data(),
                                       encode_contexts.size()));
    for (const auto& token : tokens) {
        ASSERT_TRUE(lzrc_carc_encode_token(&encoder, &token));
    }
    ASSERT_TRUE(lzrc_carc_encoder_finish(&encoder));
    encoded.resize(lzrc_carc_encoder_size(&encoder));

    std::vector<lzrc_context> decode_contexts(lzrc_carc_context_count(profile));
    lzrc_carc_decoder decoder;
    ASSERT_TRUE(lzrc_carc_decoder_init(&decoder, profile, encoded.data(),
                                       encoded.size(), decode_contexts.data(),
                                       decode_contexts.size()));
    for (const auto& expected : tokens) {
        lzrc_token decoded{};
        ASSERT_TRUE(lzrc_carc_decode_token(&decoder, &decoded));
        EXPECT_EQ(decoded.type, expected.type);
        EXPECT_EQ(decoded.literal, expected.literal);
        EXPECT_EQ(decoded.length, expected.length);
        EXPECT_EQ(decoded.offset, expected.offset);
    }
}

}  // namespace

TEST(CarcContextTest, UsesProfileSpecificContextCounts) {
    EXPECT_EQ(lzrc_carc_context_count(0U), 527U);
    EXPECT_EQ(lzrc_carc_context_count(1U), 1037U);
    EXPECT_EQ(lzrc_carc_context_count(2U), 1292U);
    EXPECT_EQ(lzrc_carc_context_count(3U), 1547U);
    EXPECT_EQ(lzrc_carc_context_count(4U), 2057U);
    EXPECT_EQ(lzrc_carc_context_count(5U), 0U);
}

TEST(CarcTest, RoundTripsTokenBoundariesForEveryProfile) {
    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        RoundTripTokens(profile);
    }
}

TEST(CarcTest, RejectsInvalidMatchesForProfile) {
    std::array<std::uint8_t, 128> output{};
    std::vector<lzrc_context> contexts(lzrc_carc_context_count(0U));

    for (const auto token : {lzrc_token_match(2U, 1U),
                             lzrc_token_match(19U, 1U),
                             lzrc_token_match(3U, 0U),
                             lzrc_token_match(3U, 257U)}) {
        lzrc_carc_encoder encoder;
        ASSERT_TRUE(lzrc_carc_encoder_init(&encoder, 0U, output.data(),
                                           output.size(), contexts.data(),
                                           contexts.size()));
        EXPECT_FALSE(lzrc_carc_encode_token(&encoder, &token));
    }
}

TEST(CarcTest, RejectsUnknownProfileAndInsufficientContextStorage) {
    std::array<std::uint8_t, 128> output{};
    std::array<lzrc_context, 526> contexts{};
    lzrc_carc_encoder encoder;
    lzrc_carc_decoder decoder;

    EXPECT_FALSE(lzrc_carc_encoder_init(&encoder, 5U, output.data(),
                                        output.size(), contexts.data(),
                                        contexts.size()));
    EXPECT_FALSE(lzrc_carc_encoder_init(&encoder, 0U, output.data(),
                                        output.size(), contexts.data(),
                                        contexts.size()));
    EXPECT_FALSE(lzrc_carc_decoder_init(&decoder, 0U, output.data(), output.size(),
                                        contexts.data(), contexts.size()));
}

TEST(CarcTest, RejectsUnusedOffsetValuesFromMalformedStreams) {
    std::array<std::uint8_t, 128> output{};
    std::vector<lzrc_context> encode_contexts(lzrc_carc_context_count(1U));
    lzrc_carc_encoder encoder;
    ASSERT_TRUE(lzrc_carc_encoder_init(&encoder, 1U, output.data(), output.size(),
                                       encode_contexts.data(),
                                       encode_contexts.size()));

    ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder.range,
                                               &encoder.model.flags[0], 1U));
    ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder.range, 0U));
    ASSERT_TRUE(lzrc_bit_tree_encode(&encoder.range, encoder.model.length_mini,
                                     4U, 0U));
    ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder.range, 1U));
    ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder.range, 0U));
    ASSERT_TRUE(lzrc_bit_tree_encode(&encoder.range, encoder.model.offsets[1],
                                     8U, 240U));
    for (unsigned int bit = 0U; bit < 4U; ++bit) {
        ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder.range, 0U));
    }
    ASSERT_TRUE(lzrc_carc_encoder_finish(&encoder));

    std::vector<lzrc_context> decode_contexts(lzrc_carc_context_count(1U));
    lzrc_carc_decoder decoder;
    ASSERT_TRUE(lzrc_carc_decoder_init(
        &decoder, 1U, output.data(), lzrc_carc_encoder_size(&encoder),
        decode_contexts.data(), decode_contexts.size()));
    lzrc_token token{};
    EXPECT_FALSE(lzrc_carc_decode_token(&decoder, &token));
}

TEST(CarcTest, RejectsUnsupportedLengthUnaryForProfile) {
    std::array<std::uint8_t, 64> output{};
    std::vector<lzrc_context> encode_contexts(lzrc_carc_context_count(0U));
    lzrc_carc_encoder encoder;
    ASSERT_TRUE(lzrc_carc_encoder_init(&encoder, 0U, output.data(), output.size(),
                                       encode_contexts.data(),
                                       encode_contexts.size()));
    ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder.range,
                                               &encoder.model.flags[0], 1U));
    ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder.range, 1U));
    ASSERT_TRUE(lzrc_carc_encoder_finish(&encoder));

    std::vector<lzrc_context> decode_contexts(lzrc_carc_context_count(0U));
    lzrc_carc_decoder decoder;
    ASSERT_TRUE(lzrc_carc_decoder_init(
        &decoder, 0U, output.data(), lzrc_carc_encoder_size(&encoder),
        decode_contexts.data(), decode_contexts.size()));
    lzrc_token token{};
    EXPECT_FALSE(lzrc_carc_decode_token(&decoder, &token));
}
