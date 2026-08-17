#include "binarytree_matchfinder.h"

#include "lzss.h"
#include "lzss_matchfinder_internal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::uint32_t Hash3(const std::vector<std::uint8_t>& input,
                    std::size_t position,
                    std::uint32_t hash_count) {
    std::uint32_t hash = input[position];
    hash = hash * 251U + input[position + 1U];
    hash = hash * 251U + input[position + 2U];
    return hash & (hash_count - 1U);
}

std::uint16_t MatchLength(const std::vector<std::uint8_t>& input,
                          std::size_t current,
                          std::size_t candidate,
                          std::uint16_t maximum_match) {
    std::size_t length = 0U;
    while (length < maximum_match && current + length < input.size() &&
           input[current + length] == input[candidate + length]) {
        ++length;
    }
    return static_cast<std::uint16_t>(length);
}

lzrc_sg_match BruteBand(const std::vector<std::uint8_t>& input,
                        std::size_t current,
                        std::uint32_t hash_count,
                        std::uint32_t minimum_offset,
                        std::uint32_t maximum_offset,
                        std::uint16_t maximum_match) {
    lzrc_sg_match best{};
    if (input.size() - current < 3U) {
        return best;
    }
    const auto hash = Hash3(input, current, hash_count);
    const auto available = std::min<std::size_t>(current, maximum_offset);
    for (std::size_t offset = minimum_offset; offset <= available; ++offset) {
        const auto candidate = current - offset;
        if (Hash3(input, candidate, hash_count) != hash) {
            continue;
        }
        const auto length =
            MatchLength(input, current, candidate, maximum_match);
        if (length > best.length ||
            (length == best.length && length >= 3U && offset < best.offset)) {
            best.length = length;
            best.offset = static_cast<std::uint32_t>(offset);
        }
    }
    return best;
}

std::vector<std::uint8_t> DeterministicBytes(std::size_t size) {
    std::vector<std::uint8_t> result(size);
    std::uint32_t state = 0xC001D00DU;
    for (auto& value : result) {
        state = state * 1664525U + 1013904223U;
        value = static_cast<std::uint8_t>(state >> 24U);
    }
    return result;
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

const lzrc_token* TokenAt(const std::vector<lzrc_token>& tokens,
                          std::size_t target) {
    std::size_t position = 0U;
    for (const auto& token : tokens) {
        if (position == target) {
            return &token;
        }
        position += (token.type == LZRC_TOKEN_LITERAL) ? 1U : token.length;
        if (position > target) {
            return nullptr;
        }
    }
    return nullptr;
}

}  // namespace

TEST(BinaryTreeDepthLimitTest, UsesExactStandardFormula) {
    EXPECT_EQ(lzrc_sg_depth_limit(1U), 0U);
    EXPECT_EQ(lzrc_sg_depth_limit(2U), 1U);
    EXPECT_EQ(lzrc_sg_depth_limit(5U), 3U);
    EXPECT_EQ(lzrc_sg_depth_limit(8U), 5U);
    EXPECT_EQ(lzrc_sg_depth_limit(16777216U), 41U);
}

TEST(BinaryTreeForestTest, MatchesBruteForceWithinEachConfiguredBand) {
    constexpr std::uint32_t kWindow = 32U;
    constexpr std::uint32_t kNear = 4U;
    constexpr std::uint32_t kFar = 12U;
    constexpr std::uint32_t kHashes = 16U;
    constexpr std::uint16_t kMaximumMatch = 8U;
    auto input = DeterministicBytes(320U);
    for (std::size_t position = 64U; position + 24U < input.size();
         position += 47U) {
        std::copy_n(input.begin() + position - 23U, 8U,
                    input.begin() + position);
    }

    auto* dictionary = lzrc_sg_dictionary_create(
        input.data(), input.size(), kWindow, kMaximumMatch, kHashes, kNear,
        kFar);
    ASSERT_NE(dictionary, nullptr);

    for (std::size_t current = 0U; current < input.size(); ++current) {
        const auto far = lzrc_sg_dictionary_query_far(dictionary, current);
        const auto expected_far = BruteBand(input, current, kHashes, kNear + 1U,
                                            kFar, kMaximumMatch);
        EXPECT_EQ(far.length, expected_far.length) << "position " << current;
        if (far.length >= 3U) {
            EXPECT_GE(far.offset, kNear + 1U);
            EXPECT_LE(far.offset, kFar);
        }

        const auto huge = lzrc_sg_dictionary_query_huge(dictionary, current);
        const auto expected_huge = BruteBand(
            input, current, kHashes, kFar + 1U, kWindow, kMaximumMatch);
        EXPECT_EQ(huge.length, expected_huge.length) << "position " << current;
        if (huge.length >= 3U) {
            EXPECT_GE(huge.offset, kFar + 1U);
            EXPECT_LE(huge.offset, kWindow);
        }

        ASSERT_TRUE(lzrc_sg_dictionary_advance(dictionary, current));
        ASSERT_TRUE(lzrc_sg_dictionary_validate(dictionary, current + 1U));
    }

    lzrc_sg_dictionary_destroy(dictionary);
}

TEST(BinaryTreeForestTest, RebuildsOrderedBucketsAndShrinkingBuckets) {
    constexpr std::uint32_t kWindow = 32U;
    constexpr std::uint32_t kNear = 2U;
    constexpr std::uint32_t kFar = 10U;
    std::vector<std::uint8_t> input(256U, 0U);
    auto tail = DeterministicBytes(128U);
    std::copy(tail.begin(), tail.end(), input.begin() + 128U);

    auto* dictionary = lzrc_sg_dictionary_create(
        input.data(), input.size(), kWindow, 8U, 16U, kNear, kFar);
    ASSERT_NE(dictionary, nullptr);
    for (std::size_t current = 0U; current < input.size(); ++current) {
        ASSERT_TRUE(lzrc_sg_dictionary_advance(dictionary, current));
        ASSERT_TRUE(lzrc_sg_dictionary_validate(dictionary, current + 1U));
    }

    const auto statistics = lzrc_sg_dictionary_statistics(dictionary);
    EXPECT_GT(statistics.insert_count, 0U);
    EXPECT_GT(statistics.delete_count, 0U);
    EXPECT_GT(statistics.subtree_rebuild_count, 0U);
    EXPECT_GT(statistics.full_rebuild_count, 0U);
    EXPECT_GT(statistics.rebuilt_node_count, 0U);
    EXPECT_LE(statistics.maximum_depth, lzrc_sg_depth_limit(kWindow));

    lzrc_sg_dictionary_destroy(dictionary);
}

TEST(BinaryTreeForestTest, FallsBackForPathologicallyConcentratedBucket) {
    constexpr std::uint32_t kWindow = 16384U;
    constexpr std::uint32_t kNear = 2U;
    constexpr std::uint32_t kFar = 4096U;
    std::vector<std::uint8_t> input(20000U, 0U);
    auto* dictionary = lzrc_sg_dictionary_create(
        input.data(), input.size(), kWindow, 8U, 16U, kNear, kFar);
    ASSERT_NE(dictionary, nullptr);

    for (std::size_t current = 0U; current < input.size(); ++current) {
        ASSERT_TRUE(lzrc_sg_dictionary_advance(dictionary, current));
    }

    const auto statistics = lzrc_sg_dictionary_statistics(dictionary);
    EXPECT_GT(statistics.fallback_bucket_count, 0U);
    lzrc_sg_dictionary_destroy(dictionary);
}

TEST(BinaryTreeIntegrationTest, Profile3FindsFarBandMatch) {
    constexpr std::size_t kTarget = 70000U;
    auto input = DeterministicBytes(kTarget + 40U);
    std::copy_n(input.begin(), 32U, input.begin() + kTarget);

    const auto tokens = Tokenize(3U, input);
    const auto* token = TokenAt(tokens, kTarget);
    ASSERT_NE(token, nullptr);
    EXPECT_EQ(token->type, LZRC_TOKEN_MATCH);
    EXPECT_EQ(token->length, 32U);
    EXPECT_EQ(token->offset, kTarget);
}

TEST(BinaryTreeIntegrationTest, Profile4FindsHugeBandMatch) {
    constexpr std::size_t kTarget = 1100000U;
    auto input = DeterministicBytes(kTarget + 48U);
    std::copy_n(input.begin(), 40U, input.begin() + kTarget);

    const auto tokens = Tokenize(4U, input);
    const auto* token = TokenAt(tokens, kTarget);
    ASSERT_NE(token, nullptr);
    EXPECT_EQ(token->type, LZRC_TOKEN_MATCH);
    EXPECT_EQ(token->length, 40U);
    EXPECT_EQ(token->offset, kTarget);
}

TEST(BinaryTreeIntegrationTest, Profile3DispatchesThroughForest) {
    constexpr std::size_t kTarget = 70000U;
    auto input = DeterministicBytes(kTarget + 40U);
    std::copy_n(input.begin(), 32U, input.begin() + kTarget);
    std::vector<lzrc_token> tokens(input.size());
    std::size_t token_count = 0U;
    lzrc_lzss_matchfinder_statistics statistics{};

    ASSERT_TRUE(lzrc_lzss_tokenize_instrumented(
        3U, input.data(), input.size(), tokens.data(), tokens.size(),
        &token_count, true, &statistics));
    EXPECT_GT(statistics.binarytree.insert_count, 0U);
    EXPECT_GT(statistics.binarytree.visited_node_count, 0U);
}
