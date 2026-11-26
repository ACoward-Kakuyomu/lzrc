#include <gtest/gtest.h>
#include "bwt.h"
#include "bit_tree.h"
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

TEST(BwtTest, DecodesHistoricalContainers) {
    const std::string original = "banana banana banana!\n";
    const std::vector<std::vector<uint8_t>> fixtures = {
        {0x4c,0x42,0x57,0x54,0x01,0x01,0,0,0x16,0,0,0,0x0d,0,0,0,
         0x16,0,0,0,0x21,0x64,0x69,0x21,0x04,0xed,0x5d,0xc8,0xb5,0xda,
         0x47,0xc5,0x06,0x36,0x01,0xcb,0xdb,0x3d,0xb2,0x92,0x6b,0x07}
        ,{0x4c,0x42,0x57,0x54,0x02,0x01,0,0,0x16,0,0,0,0x0d,0,0,0,
          0x18,0,0,0,0x21,0x61,0x61,0x60,0xe0,0xd7,0x07,0xe6,0xb4,0xa8,
          0x42,0x7e,0xf3,0x2f,0x33,0x20,0x8a,0x91,0x8b,0x02,0x1c,0xa7,0x5b,0x1f}
        ,{0x4c,0x42,0x57,0x54,0x03,0x01,0,0,0x16,0,0,0,0x0d,0,0,0,
          0x11,0,0,0,0x90,0xd7,0xe8,0x24,0x6a,0xe9,0xd3,0x8a,0x5c,0x7a,
          0xfa,0x77,0xfa,0x87,0xa8,0x30,0x00}
    };
    for (const auto& compressed : fixtures) {
        std::vector<uint8_t> restored(original.size());
        size_t written = 0;
        ASSERT_EQ(lzrc_bwt_decompress(compressed.data(), compressed.size(), original.size(),
                                     restored.data(), restored.size(), &written), LZRC_OK);
        EXPECT_EQ(std::string(restored.begin(), restored.end()), original);
    }
}

TEST(BwtTest, OrderOneUsesPreviousByteToSelectTree) {
    const std::vector<uint8_t> last = {2, 3, 2, 3, 2, 4, 2, 3, 0, 2, 3};
    std::vector<lzrc_context> contexts(256U * 255U);
    lzrc_contexts_init(contexts.data(), contexts.size());
    std::array<uint8_t, 256> reference{}, actual{};
    lzrc_range_encoder encoder;
    ASSERT_TRUE(lzrc_range_encoder_init(&encoder, 4, reference.data(), reference.size()));
    uint8_t previous = 0;
    for (uint8_t value : last) {
        ASSERT_TRUE(lzrc_bit_tree_encode(&encoder, contexts.data() + previous * 255U, 8, value));
        previous = value;
    }
    ASSERT_TRUE(lzrc_range_encoder_finish(&encoder));
    size_t written = 0;
    ASSERT_EQ(lzrc_bwt_encode_block(2, last.data(), last.size(), actual.data(),
                                   actual.size(), &written), LZRC_OK);
    EXPECT_EQ(written, lzrc_range_encoder_size(&encoder));
    EXPECT_TRUE(std::equal(actual.begin(), actual.begin() + written, reference.begin()));
}

TEST(BwtTest, MtfKnownRanksAndInPlaceInverse) {
    const std::vector<uint8_t> input = {2, 2, 1, 2, 0, 0};
    std::vector<uint8_t> ranks(input.size());
    lzrc_mtf_forward(input.data(), input.size(), ranks.data());
    EXPECT_EQ(ranks, (std::vector<uint8_t>{2, 0, 2, 1, 2, 0}));
    lzrc_mtf_inverse(ranks.data(), ranks.size(), ranks.data());
    EXPECT_EQ(ranks, input);
    std::vector<uint8_t> alphabet(256);
    std::iota(alphabet.begin(), alphabet.end(), uint8_t{0});
    auto original = alphabet;
    lzrc_mtf_forward(alphabet.data(), alphabet.size(), alphabet.data());
    lzrc_mtf_inverse(alphabet.data(), alphabet.size(), alphabet.data());
    EXPECT_EQ(alphabet, original);
}

TEST(BwtTest, FixedZeroRunMatchesUnchangingProbability) {
    std::vector<uint8_t> zeros(1024, 0);
    std::array<uint8_t, 4096> reference{}, actual{};
    lzrc_range_encoder encoder;
    ASSERT_TRUE(lzrc_range_encoder_init(&encoder, 4, reference.data(), reference.size()));
    lzrc_context flag{128};
    ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder, &flag, 0));
    for (size_t i = 1; i <= zeros.size(); ++i) {
        lzrc_context fixed{255}; // Discard adaptation, recreate at every bit.
        ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder, &fixed,
                                                  i == zeros.size() ? 1 : 0));
    }
    ASSERT_TRUE(lzrc_range_encoder_finish(&encoder));
    size_t written = 0;
    ASSERT_EQ(lzrc_bwt_encode_block(3, zeros.data(), zeros.size(), actual.data(),
                                   actual.size(), &written), LZRC_OK);
    ASSERT_EQ(written, lzrc_range_encoder_size(&encoder));
    EXPECT_TRUE(std::equal(actual.begin(), actual.begin() + written, reference.begin()));
    std::vector<uint8_t> restored(zeros.size());
    EXPECT_EQ(lzrc_bwt_decode_block(3, actual.data(), written, restored.data(), restored.size()),
              LZRC_OK);
    EXPECT_EQ(restored, zeros);
}

TEST(BwtTest, MtfRejectsOverlongRunAndZeroInNonzeroTree) {
    for (bool zero_run : {false, true}) {
        std::array<uint8_t, 64> coded{};
        lzrc_range_encoder encoder;
        ASSERT_TRUE(lzrc_range_encoder_init(&encoder, 4, coded.data(), coded.size()));
        lzrc_context flag{128};
        ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder, &flag, zero_run ? 0 : 1));
        if (zero_run) {
            // Four zeros cannot fit in the declared three-byte block.
            for (int i = 0; i < 3; ++i)
                ASSERT_TRUE(lzrc_range32_encode_fixed_bit(&encoder.state.range32, 255, 0));
            ASSERT_TRUE(lzrc_range32_encode_fixed_bit(&encoder.state.range32, 255, 1));
        } else {
            std::array<lzrc_context, 255> tree;
            lzrc_contexts_init(tree.data(), tree.size());
            ASSERT_TRUE(lzrc_bit_tree_encode(&encoder, tree.data(), 8, 0));
        }
        ASSERT_TRUE(lzrc_range_encoder_finish(&encoder));
        std::array<uint8_t, 3> out{};
        EXPECT_EQ(lzrc_bwt_decode_block(3, coded.data(), lzrc_range_encoder_size(&encoder),
                                       out.data(), out.size()), LZRC_ERROR_CORRUPT_INPUT);
    }
}

TEST(BwtTest, ZeroRunLengthsIncludingMaximumBlock) {
    for (unsigned int stage = 3; stage <= LZRC_BWT_STAGE; ++stage) {
        for (size_t n : {1U, 2U, 3U, 31U, 32U, 255U, 256U, 1023U,
                         1024U, 65535U, 131072U, 1048576U}) {
            std::vector<uint8_t> last(n, 0), coded(32 * n + 64), decoded(n);
            size_t written = 0;
            ASSERT_EQ(lzrc_bwt_encode_block(stage, last.data(), n, coded.data(),
                                           coded.size(), &written), LZRC_OK);
            ASSERT_EQ(lzrc_bwt_decode_block(stage, coded.data(), written, decoded.data(), n),
                      LZRC_OK) << stage << ":" << n;
            EXPECT_EQ(decoded, last);
        }
    }
}

TEST(BwtTest, GammaMatchesKnownCodewords) {
    const std::vector<std::pair<size_t, std::string>> cases = {
        {1, "1"}, {2, "010"}, {3, "011"}, {4, "00100"}, {5, "00101"},
        {7, "00111"}, {8, "0001000"}, {15, "0001111"}, {16, "000010000"}
    };
    for (const auto& item : cases) {
        std::array<uint8_t, 128> reference{}, actual{};
        lzrc_range_encoder encoder;
        ASSERT_TRUE(lzrc_range_encoder_init(&encoder, 4, reference.data(), reference.size()));
        lzrc_context flag{128};
        ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder, &flag, 0));
        for (char bit : item.second)
            ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder, bit == '1' ? 1 : 0));
        ASSERT_TRUE(lzrc_range_encoder_finish(&encoder));
        std::vector<uint8_t> last(item.first, 0);
        size_t written = 0;
        ASSERT_EQ(lzrc_bwt_encode_block(4, last.data(), last.size(), actual.data(),
                                       actual.size(), &written), LZRC_OK);
        ASSERT_EQ(written, lzrc_range_encoder_size(&encoder));
        EXPECT_TRUE(std::equal(actual.begin(), actual.begin() + written, reference.begin()));
    }
}

TEST(BwtTest, GammaRejectsOverlongValueAndPrefix) {
    for (const std::string bits : {"00101", "000000000000000000000000000000"}) {
        std::array<uint8_t, 128> coded{}, decoded{};
        lzrc_range_encoder encoder;
        ASSERT_TRUE(lzrc_range_encoder_init(&encoder, 4, coded.data(), coded.size()));
        lzrc_context flag{128};
        ASSERT_TRUE(lzrc_range_encoder_encode_bit(&encoder, &flag, 0));
        for (char bit : bits)
            ASSERT_TRUE(lzrc_range_encoder_encode_direct_bit(&encoder, bit == '1' ? 1 : 0));
        ASSERT_TRUE(lzrc_range_encoder_finish(&encoder));
        EXPECT_EQ(lzrc_bwt_decode_block(4, coded.data(), lzrc_range_encoder_size(&encoder),
                                       decoded.data(), 4), LZRC_ERROR_CORRUPT_INPUT);
    }
}

TEST(BwtTest, GammaImprovesVeryLongRunButUnaryWinsForMediumRun) {
    for (size_t n : {256U, 1048576U}) {
        std::vector<uint8_t> last(n, 0), coded(32 * n + 64);
        size_t unary_size = 0, gamma_size = 0;
        ASSERT_EQ(lzrc_bwt_encode_block(3, last.data(), n, coded.data(), coded.size(),
                                       &unary_size), LZRC_OK);
        ASSERT_EQ(lzrc_bwt_encode_block(4, last.data(), n, coded.data(), coded.size(),
                                       &gamma_size), LZRC_OK);
        if (n == 256) EXPECT_LT(unary_size, gamma_size);
        else EXPECT_LT(gamma_size, unary_size);
    }
}
