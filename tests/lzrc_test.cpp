#include "lzrc/lzrc.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::uint8_t> Compress(unsigned int profile,
                                   const std::vector<std::uint8_t>& input) {
    std::size_t bound = 0U;
    EXPECT_EQ(lzrc_compress_bound(input.size(), &bound), LZRC_OK);
    std::vector<std::uint8_t> encoded(bound);
    std::size_t encoded_size = 0U;
    EXPECT_EQ(lzrc_compress(profile, input.data(), input.size(), encoded.data(),
                            encoded.size(), &encoded_size),
              LZRC_OK);
    encoded.resize(encoded_size);
    return encoded;
}

void ExpectRoundTrip(unsigned int profile,
                     const std::vector<std::uint8_t>& input) {
    const auto encoded = Compress(profile, input);
    std::vector<std::uint8_t> decoded(input.size());
    std::size_t decoded_size = 0U;
    ASSERT_EQ(lzrc_decompress(profile, encoded.data(), encoded.size(), input.size(),
                              decoded.data(), decoded.size(), &decoded_size),
              LZRC_OK);
    decoded.resize(decoded_size);
    EXPECT_EQ(decoded, input);
}

std::vector<std::uint8_t> RandomBytes(std::size_t size) {
    std::vector<std::uint8_t> result;
    std::uint32_t state = 0xBADC0FFEU;
    result.reserve(size);
    for (std::size_t index = 0U; index < size; ++index) {
        state = state * 1664525U + 1013904223U;
        result.push_back(static_cast<std::uint8_t>(state >> 24U));
    }
    return result;
}

}  // namespace

TEST(LzrcTest, RoundTripsEmptyAndSingleByteForEveryProfile) {
    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        ExpectRoundTrip(profile, {});
        ExpectRoundTrip(profile, {0xA5U});
    }
}

TEST(LzrcTest, RoundTripsRandomAndHighlyRedundantDataForEveryProfile) {
    auto data = RandomBytes(8192U);
    const std::string phrase = "LZRC makes the side story real. ";
    for (unsigned int repeat = 0U; repeat < 200U; ++repeat) {
        data.insert(data.end(), phrase.begin(), phrase.end());
    }
    data.insert(data.end(), 2048U, 0x42U);

    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        ExpectRoundTrip(profile, data);
    }
}

TEST(LzrcTest, ReportsInvalidArgumentsProfilesAndSmallBuffers) {
    std::array<std::uint8_t, 64> buffer{};
    std::size_t size = 0U;
    EXPECT_EQ(lzrc_compress_bound(SIZE_MAX, &size), LZRC_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(lzrc_compress_bound(1U, nullptr), LZRC_ERROR_INVALID_ARGUMENT);
    EXPECT_EQ(lzrc_compress(5U, buffer.data(), 1U, buffer.data(), buffer.size(),
                            &size),
              LZRC_ERROR_INVALID_PROFILE);
    EXPECT_EQ(lzrc_compress(0U, buffer.data(), 1U, buffer.data(), 1U, &size),
              LZRC_ERROR_OUTPUT_TOO_SMALL);
    EXPECT_EQ(lzrc_decompress(0U, buffer.data(), buffer.size(), 65U,
                              buffer.data(), buffer.size(), &size),
              LZRC_ERROR_OUTPUT_TOO_SMALL);
}

TEST(LzrcTest, RejectsTruncatedStreamsAndOutputSizeMismatch) {
    const std::vector<std::uint8_t> data(64U, 0x33U);
    const auto encoded = Compress(0U, data);
    std::vector<std::uint8_t> decoded(data.size());
    std::size_t decoded_size = 0U;

    EXPECT_EQ(lzrc_decompress(0U, encoded.data(), 1U, data.size(), decoded.data(),
                              decoded.size(), &decoded_size),
              LZRC_ERROR_CORRUPT_INPUT);
    EXPECT_EQ(lzrc_decompress(0U, encoded.data(), encoded.size(), data.size() - 1U,
                              decoded.data(), decoded.size(), &decoded_size),
              LZRC_ERROR_CORRUPT_INPUT);
}

TEST(LzrcTest, CompressionIsDeterministic) {
    const auto data = RandomBytes(4096U);
    for (unsigned int profile = 0U; profile <= 4U; ++profile) {
        EXPECT_EQ(Compress(profile, data), Compress(profile, data));
    }
}

TEST(LzrcTest, RoundTripsDeterministicRandomBoundarySizes) {
    const std::array<std::size_t, 19> sizes{
        0U,   1U,   2U,   3U,   17U,  18U,  19U,  255U, 256U, 257U,
        273U, 274U, 275U, 529U, 530U, 531U, 4095U, 4096U, 4097U};
    for (const auto size : sizes) {
        const auto data = RandomBytes(size);
        for (unsigned int profile = 0U; profile <= 4U; ++profile) {
            ExpectRoundTrip(profile, data);
        }
    }
}

TEST(LzrcResultTest, ProvidesStableMessages) {
    EXPECT_STREQ(lzrc_result_string(LZRC_OK), "success");
    EXPECT_STREQ(lzrc_result_string(LZRC_ERROR_CORRUPT_INPUT),
                 "corrupt or truncated input");
}
