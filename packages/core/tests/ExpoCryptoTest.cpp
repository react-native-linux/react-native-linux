#include "ExpoCrypto.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {
namespace {

constexpr std::size_t kRandomBytes = 64;
constexpr std::size_t kUuidDraws = 256;

struct DigestVector {
    std::string_view algorithm;
    std::string_view message;
    std::string_view hex;
};

/** RFC 1321 A.5 for MD5, FIPS 180-2 appendices A to D for the SHA family. */
constexpr std::array<DigestVector, 8> kDigestVectors = {{
    {"MD5", "", "d41d8cd98f00b204e9800998ecf8427e"},
    {"MD5", "abc", "900150983cd24fb0d6963f7d28e17f72"},
    {"SHA-1", "abc", "a9993e364706816aba3e25717850c26c9cd0d89d"},
    {"SHA-256", "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"SHA-256", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    {"SHA-384", "abc",
     "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"},
    {"SHA-512", "abc",
     "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
     "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    {"SHA-512", "",
     "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
     "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
}};

TEST(ExpoCryptoTest, DigestsMatchTheStandardsVectorsInHex) {
    for (const DigestVector& vector : kDigestVectors) {
        EXPECT_EQ(encodedCryptoDigest(vector.algorithm, vector.message, "hex"), vector.hex) << vector.algorithm;
    }
}

TEST(ExpoCryptoTest, Base64IsPaddedAndUnwrapped) {
    EXPECT_EQ(encodedCryptoDigest("SHA-256", "abc", "base64"), "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=");
}

TEST(ExpoCryptoTest, StringsAreDigestedAsTheirUtf8Bytes) {
    EXPECT_EQ(encodedCryptoDigest("SHA-256", "\xc3\xa9\xe2\x9c\x93", "hex"),
              "1e77cb71920ad52885fab06465a5fcf2e5f98c0382aab36202689431b93b9fb3");
}

TEST(ExpoCryptoTest, BinaryInputIsDigestedByteForByte) {
    const std::array<std::uint8_t, 3> message = {'a', 'b', 'c'};
    const std::vector<std::uint8_t> digest = cryptoDigest("SHA-1", message);

    ASSERT_EQ(digest.size(), 20U);
    EXPECT_EQ(digest.front(), 0xa9);
    EXPECT_EQ(digest.back(), 0x9d);
}

TEST(ExpoCryptoTest, LegacyAndUnknownAlgorithmsAreRefused) {
    for (const std::string_view algorithm : {"MD2", "MD4", "SHA-224", "sha-256", "SHA3-256", ""}) {
        EXPECT_THROW(static_cast<void>(cryptoDigest(algorithm, {})), std::invalid_argument) << algorithm;
    }
}

TEST(ExpoCryptoTest, UnknownEncodingsAreRefused) {
    EXPECT_THROW(static_cast<void>(encodedCryptoDigest("SHA-256", "abc", "base32")), std::invalid_argument);
}

TEST(ExpoCryptoTest, RandomBytesFillTheWholeSpanAndDiffer) {
    std::array<std::uint8_t, kRandomBytes> first{};
    std::array<std::uint8_t, kRandomBytes> second{};

    fillWithRandomBytes(first);
    fillWithRandomBytes(second);
    fillWithRandomBytes({});

    EXPECT_NE(first, second);
    EXPECT_TRUE(std::ranges::any_of(first, [](std::uint8_t byte) { return byte != 0; }));
}

TEST(ExpoCryptoTest, UuidsAreLowercaseVersion4AndUnique) {
    const std::regex version4("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
    std::set<std::string> drawn;

    for (std::size_t draw = 0; draw < kUuidDraws; ++draw) {
        const std::string uuid = randomUuid();

        EXPECT_TRUE(std::regex_match(uuid, version4)) << uuid;
        drawn.insert(uuid);
    }

    EXPECT_EQ(drawn.size(), kUuidDraws);
}

} // namespace
} // namespace react_native_linux
