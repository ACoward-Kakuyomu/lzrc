#include "lzss.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::uint8_t> MarkerAtDistance(std::size_t distance) {
    std::vector<std::uint8_t> input(distance + 4U, 0U);
    input[0] = 0xF1U;
    input[1] = 0x37U;
    input[2] = 0xC9U;
    input[3] = 0x6BU;
    input[distance] = 0xF1U;
    input[distance + 1U] = 0x37U;
    input[distance + 2U] = 0xC9U;
    input[distance + 3U] = 0x6BU;
    return input;
}

std::vector<lzrc_token> TokenizeBoundary(unsigned int profile,
                                         const std::vector<std::uint8_t>& input,
                                         std::uint16_t maximum_length) {
    const std::size_t capacity =
        input.size() / std::max<std::size_t>(maximum_length, 3U) + 32U;
    std::vector<lzrc_token> tokens(capacity);
    std::size_t token_count = 0U;
    EXPECT_TRUE(lzrc_lzss_tokenize(profile, input.data(), input.size(),
                                   tokens.data(), tokens.size(), &token_count));
    tokens.resize(token_count);
    return tokens;
}

}  // namespace

TEST(ProfileBoundaryTest, AcceptsMaximumWindowOffsetAndRejectsNextOffset) {
    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        lzrc_profile_parameters parameters{};
        ASSERT_TRUE(lzrc_profile_get(profile, &parameters));

        const auto at_limit = MarkerAtDistance(parameters.window_size);
        const auto limit_tokens =
            TokenizeBoundary(profile, at_limit,
                             parameters.maximum_match_length);
        ASSERT_FALSE(limit_tokens.empty());
        EXPECT_EQ(limit_tokens.back().type, LZRC_TOKEN_MATCH)
            << "profile=" << profile;
        EXPECT_EQ(limit_tokens.back().length, 4U) << "profile=" << profile;
        EXPECT_EQ(limit_tokens.back().offset, parameters.window_size)
            << "profile=" << profile;

        const auto outside = MarkerAtDistance(
            static_cast<std::size_t>(parameters.window_size) + 1U);
        const auto outside_tokens =
            TokenizeBoundary(profile, outside,
                             parameters.maximum_match_length);
        ASSERT_GE(outside_tokens.size(), 4U);
        EXPECT_EQ(outside_tokens[outside_tokens.size() - 4U].type,
                  LZRC_TOKEN_LITERAL)
            << "profile=" << profile;
        EXPECT_EQ(outside_tokens[outside_tokens.size() - 3U].type,
                  LZRC_TOKEN_LITERAL)
            << "profile=" << profile;
        EXPECT_EQ(outside_tokens[outside_tokens.size() - 2U].type,
                  LZRC_TOKEN_LITERAL)
            << "profile=" << profile;
        EXPECT_EQ(outside_tokens.back().type, LZRC_TOKEN_LITERAL)
            << "profile=" << profile;
    }
}

TEST(ProfileBoundaryTest, SplitsAtEveryMaximumMatchLength) {
    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        lzrc_profile_parameters parameters{};
        ASSERT_TRUE(lzrc_profile_get(profile, &parameters));
        std::vector<std::uint8_t> input(
            static_cast<std::size_t>(parameters.maximum_match_length) + 4U,
            0x6DU);
        std::vector<lzrc_token> tokens(input.size());
        std::size_t token_count = 0U;
        ASSERT_TRUE(lzrc_lzss_tokenize(profile, input.data(), input.size(),
                                       tokens.data(), tokens.size(),
                                       &token_count));
        ASSERT_GE(token_count, 2U);
        EXPECT_EQ(tokens[0].type, LZRC_TOKEN_LITERAL);
        EXPECT_EQ(tokens[1].type, LZRC_TOKEN_MATCH);
        EXPECT_EQ(tokens[1].length, parameters.maximum_match_length);
        EXPECT_EQ(tokens[1].offset, 1U);
    }
}
