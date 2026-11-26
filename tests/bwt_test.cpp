#include <gtest/gtest.h>
#include "bwt.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {
std::vector<uint8_t> pack(const std::vector<uint8_t>& data, unsigned int blocks = 1U) {
    size_t capacity = 0, written = 0;
    EXPECT_EQ(lzrc_bwt_compress_bound(data.size(), blocks, &capacity), LZRC_OK);
    std::vector<uint8_t> out(capacity);
    EXPECT_EQ(lzrc_bwt_compress(blocks, data.data(), data.size(), out.data(),
                               out.size(), &written), LZRC_OK);
    out.resize(written);
    return out;
}
void roundtrip(const std::vector<uint8_t>& data, unsigned int blocks = 1U) {
    auto compressed = pack(data, blocks);
    std::vector<uint8_t> restored(data.size());
    size_t written = 0;
    ASSERT_EQ(lzrc_bwt_decompress(compressed.data(), compressed.size(), data.size(),
                                 restored.data(), restored.size(), &written), LZRC_OK);
    EXPECT_EQ(written, data.size());
    EXPECT_EQ(restored, data);
    EXPECT_EQ(pack(data, blocks), compressed);
}
}

TEST(BwtTest, KnownRotationAndInverse) {
    const std::string input = "banana";
    std::vector<uint8_t> last(input.size()), restored(input.size());
    uint32_t primary = 0;
    ASSERT_EQ(lzrc_bwt_forward(reinterpret_cast<const uint8_t*>(input.data()),
                               input.size(), last.data(), &primary), LZRC_OK);
    EXPECT_EQ(std::string(last.begin(), last.end()), "nnbaaa");
    EXPECT_EQ(primary, 3U);
    ASSERT_EQ(lzrc_bwt_inverse(last.data(), last.size(), primary, restored.data()), LZRC_OK);
    EXPECT_EQ(std::string(restored.begin(), restored.end()), input);
}

TEST(BwtTest, MatchesNaiveSortedRotations) {
    std::mt19937 rng(1989);
    for (size_t n = 1; n <= 129; ++n) {
        std::vector<uint8_t> input(n), last(n), restored(n);
        for (auto& value : input) value = static_cast<uint8_t>(rng() % 5);
        std::vector<size_t> rows(n);
        std::iota(rows.begin(), rows.end(), 0);
        auto less = [&](size_t a, size_t b) {
            for (size_t k = 0; k < n; ++k) {
                if (input[(a + k) % n] != input[(b + k) % n])
                    return input[(a + k) % n] < input[(b + k) % n];
            }
            return false;
        };
        std::stable_sort(rows.begin(), rows.end(), less);
        uint32_t primary = 0;
        ASSERT_EQ(lzrc_bwt_forward(input.data(), n, last.data(), &primary), LZRC_OK);
        for (size_t i = 0; i < n; ++i) EXPECT_EQ(last[i], input[(rows[i] + n - 1) % n]);
        ASSERT_EQ(lzrc_bwt_inverse(last.data(), n, primary, restored.data()), LZRC_OK);
        EXPECT_EQ(restored, input);
    }
}

TEST(BwtTest, EmptyAllBytesPeriodicAndRandomRoundTrips) {
    roundtrip({});
    roundtrip({0});
    roundtrip(std::vector<uint8_t>(4096, 255));
    std::vector<uint8_t> data(8192);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
    roundtrip(data);
    std::mt19937 rng(1989);
    for (auto& value : data) value = static_cast<uint8_t>(rng());
    roundtrip(data);
}

TEST(BwtTest, EveryBlockSizeAndPartialFinalBlock) {
    std::mt19937 rng(1994);
    for (unsigned int blocks = 1; blocks <= 8; ++blocks) {
        std::vector<uint8_t> data(blocks * LZRC_BWT_BLOCK_UNIT + 17U);
        for (auto& value : data) value = static_cast<uint8_t>(rng() % 16);
        roundtrip(data, blocks);
    }
}

TEST(BwtTest, RejectsInvalidArgumentsAndShortOutput) {
    size_t written = 123, bound = 0;
    uint8_t data[128] = {0};
    EXPECT_EQ(lzrc_bwt_compress_bound(10, 0, &bound), LZRC_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(lzrc_bwt_compress_bound(10, 9, &bound), LZRC_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(lzrc_bwt_compress_bound(10, 1, nullptr), LZRC_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(lzrc_bwt_compress(1, nullptr, 1, data, sizeof(data), &written),
              LZRC_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(written, 0U);
    EXPECT_EQ(lzrc_bwt_compress(1, data, 1, data + 32, 11, &written),
              LZRC_ERROR_OUTPUT_TOO_SMALL);
    auto compressed = pack({1, 2, 3});
    EXPECT_EQ(lzrc_bwt_decompress(compressed.data(), compressed.size(), 3, data, 2,
                                 &written), LZRC_ERROR_OUTPUT_TOO_SMALL);
    EXPECT_EQ(lzrc_bwt_decompress(compressed.data(), compressed.size(), 4, data, 4,
                                 &written), LZRC_ERROR_CORRUPT_INPUT);
}

TEST(BwtTest, RejectsTruncationBadHeadersAndTrailingBytes) {
    auto good = pack({0, 1, 0, 2, 0, 3});
    std::array<uint8_t, 6> restored{};
    size_t written = 0;
    for (size_t n = 0; n < good.size(); ++n) {
        EXPECT_EQ(lzrc_bwt_decompress(good.data(), n, restored.size(),
                                     restored.data(), restored.size(), &written),
                  LZRC_ERROR_CORRUPT_INPUT) << n;
    }
    for (size_t index : {size_t(0), size_t(4), size_t(5), size_t(6), size_t(7),
                         size_t(12), size_t(16)}) {
        auto bad = good;
        bad[index] = 255;
        EXPECT_EQ(lzrc_bwt_decompress(bad.data(), bad.size(), restored.size(),
                                     restored.data(), restored.size(), &written),
                  LZRC_ERROR_CORRUPT_INPUT) << index;
    }
    good.push_back(0);
    EXPECT_EQ(lzrc_bwt_decompress(good.data(), good.size(), restored.size(),
                                 restored.data(), restored.size(), &written),
              LZRC_ERROR_CORRUPT_INPUT);
}
