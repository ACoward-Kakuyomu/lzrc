#include "lzss.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::uint8_t> Bytes(const std::string& value) {
    return {value.begin(), value.end()};
}

std::vector<lzrc_token> Tokenize(unsigned int profile,
                                 const std::vector<std::uint8_t>& input) {
    std::vector<lzrc_token> tokens(std::max<std::size_t>(input.size(), 1U));
    std::size_t token_count = 0U;
    EXPECT_TRUE(lzrc_lzss_tokenize(profile, input.data(), input.size(),
                                   tokens.data(), tokens.size(), &token_count));
    tokens.resize(token_count);
    return tokens;
}

void ExpectTokenRoundTrip(unsigned int profile,
                          const std::vector<std::uint8_t>& input) {
    const auto tokens = Tokenize(profile, input);
    std::vector<std::uint8_t> decoded(input.size());
    std::size_t decoded_size = 0U;
    ASSERT_TRUE(lzrc_lzss_detokenize(profile, tokens.data(), tokens.size(),
                                     decoded.data(), decoded.size(),
                                     &decoded_size));
    decoded.resize(decoded_size);
    EXPECT_EQ(decoded, input);
}

}  // namespace

TEST(LzssProfileTest, ReportsSpecifiedLimits) {
    const std::array<std::uint32_t, 5> windows{
        256U, 4096U, 65536U, 1048576U, 16777216U};
    const std::array<std::uint16_t, 5> lengths{18U, 274U, 274U, 274U, 530U};
    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        lzrc_profile_parameters parameters{};
        ASSERT_TRUE(lzrc_profile_get(profile, &parameters));
        EXPECT_EQ(parameters.window_size, windows[profile]);
        EXPECT_EQ(parameters.maximum_match_length, lengths[profile]);
    }
    lzrc_profile_parameters parameters{};
    EXPECT_FALSE(lzrc_profile_get(5U, &parameters));
}

TEST(LzssProfile0Test, EmitsBasicLiteralAndMatchSequence) {
    const auto tokens = Tokenize(0U, Bytes("abcabc"));
    ASSERT_EQ(tokens.size(), 4U);
    EXPECT_EQ(tokens[0].literal, 'a');
    EXPECT_EQ(tokens[1].literal, 'b');
    EXPECT_EQ(tokens[2].literal, 'c');
    EXPECT_EQ(tokens[3].type, LZRC_TOKEN_MATCH);
    EXPECT_EQ(tokens[3].length, 3U);
    EXPECT_EQ(tokens[3].offset, 3U);
}

TEST(LzssProfile0Test, ChoosesFirstValidOldestCandidate) {
    const auto tokens = Tokenize(0U, Bytes("abcQabcRabc"));
    ASSERT_GE(tokens.size(), 1U);
    const auto& last = tokens.back();
    EXPECT_EQ(last.type, LZRC_TOKEN_MATCH);
    EXPECT_EQ(last.length, 3U);
    EXPECT_EQ(last.offset, 8U);
}

TEST(LzssHigherProfileTest, ChoosesLongestThenNearestCandidate) {
    const auto tokens = Tokenize(1U, Bytes("abcQabcRabcR"));
    ASSERT_GE(tokens.size(), 1U);
    const auto& last = tokens.back();
    EXPECT_EQ(last.type, LZRC_TOKEN_MATCH);
    EXPECT_EQ(last.length, 4U);
    EXPECT_EQ(last.offset, 4U);
}

TEST(LzssTest, SupportsOverlappingMatchesAndMaximumLengthSplits) {
    std::vector<std::uint8_t> repeated(600U, 0xA5U);
    const auto profile0 = Tokenize(0U, repeated);
    ASSERT_GE(profile0.size(), 2U);
    EXPECT_EQ(profile0[0].type, LZRC_TOKEN_LITERAL);
    EXPECT_EQ(profile0[1].type, LZRC_TOKEN_MATCH);
    EXPECT_EQ(profile0[1].offset, 1U);
    EXPECT_EQ(profile0[1].length, 18U);

    const auto profile4 = Tokenize(4U, repeated);
    ASSERT_GE(profile4.size(), 2U);
    EXPECT_EQ(profile4[1].offset, 1U);
    EXPECT_EQ(profile4[1].length, 530U);
}

TEST(LzssTest, RoundTripsRepresentativeDataForEveryProfile) {
    std::vector<std::uint8_t> data;
    std::uint32_t state = 0x12345678U;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        state = state * 1103515245U + 12345U;
        data.push_back(static_cast<std::uint8_t>(state >> 24U));
    }
    const auto repeated = Bytes("LZRC-LZRC-LZRC-LZRC-");
    data.insert(data.end(), repeated.begin(), repeated.end());
    data.insert(data.end(), 600U, 0x5AU);

    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        ExpectTokenRoundTrip(profile, data);
    }
}

TEST(LzssTest, RejectsInvalidTokenStreamsAndSmallBuffers) {
    std::array<std::uint8_t, 8> output{};
    std::size_t output_size = 0U;
    const std::array<lzrc_token, 1> before_start{lzrc_token_match(3U, 1U)};
    EXPECT_FALSE(lzrc_lzss_detokenize(0U, before_start.data(),
                                      before_start.size(), output.data(),
                                      output.size(), &output_size));

    const std::array<lzrc_token, 2> too_long{
        lzrc_token_literal('x'), lzrc_token_match(18U, 1U)};
    EXPECT_FALSE(lzrc_lzss_detokenize(0U, too_long.data(), too_long.size(),
                                      output.data(), output.size(),
                                      &output_size));

    const auto input = Bytes("abcdef");
    std::array<lzrc_token, 5> too_few_tokens{};
    std::size_t token_count = 0U;
    EXPECT_FALSE(lzrc_lzss_tokenize(0U, input.data(), input.size(),
                                    too_few_tokens.data(), too_few_tokens.size(),
                                    &token_count));
}
