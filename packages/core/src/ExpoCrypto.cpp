#include "ExpoCrypto.h"

#include "ExpoFileSystem.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <string>

namespace react_native_linux {
namespace {

constexpr std::array<std::string_view, 5> kDigestAlgorithms = {"MD5", "SHA-1", "SHA-256", "SHA-384", "SHA-512"};
constexpr std::string_view kHexDigits = "0123456789abcdef";
constexpr std::size_t kUuidBytes = 16;
constexpr std::size_t kUuidVersionByte = 6;
constexpr std::size_t kUuidVariantByte = 8;
constexpr std::array<std::size_t, 4> kUuidHyphenBeforeByte = {4, 6, 8, 10};

void ensure(bool succeeded) {
    if (!succeeded) // COV_EXCL: libcrypto fails these calls only without an entropy source or memory
        throw std::runtime_error("libcrypto failed an expo-crypto operation"); // COV_EXCL: see the line above
}

void appendHex(std::string& output, std::uint8_t byte) {
    output += kHexDigits[byte >> 4U];
    output += kHexDigits[byte & 0x0fU];
}

} // namespace

std::vector<std::uint8_t> cryptoDigest(std::string_view algorithm, std::span<const std::uint8_t> data) {
    if (std::ranges::find(kDigestAlgorithms, algorithm) == kDigestAlgorithms.end()) {
        throw std::invalid_argument("expo-crypto on Linux has no digest algorithm '" + std::string(algorithm) + "'");
    }

    std::vector<std::uint8_t> digest(EVP_MAX_MD_SIZE);
    std::size_t length = 0;

    ensure(EVP_Q_digest(nullptr, std::string(algorithm).c_str(), nullptr, data.data(), data.size(), digest.data(),
                        &length) == 1);
    digest.resize(length);

    return digest;
}

std::string encodedCryptoDigest(std::string_view algorithm, std::string_view data, std::string_view encoding) {
    const std::vector<std::uint8_t> digest =
        cryptoDigest(algorithm, {reinterpret_cast<const std::uint8_t*>(data.data()), data.size()});

    if (encoding == "base64") {
        return encodeBase64({reinterpret_cast<const char*>(digest.data()), digest.size()});
    }

    if (encoding != "hex") {
        throw std::invalid_argument("expo-crypto has no digest encoding '" + std::string(encoding) + "'");
    }

    std::string hex;

    for (const std::uint8_t byte : digest) {
        appendHex(hex, byte);
    }

    return hex;
}

void fillWithRandomBytes(std::span<std::uint8_t> bytes) {
    ensure(RAND_bytes_ex(nullptr, bytes.data(), bytes.size(), 0) == 1);
}

std::string randomUuid() {
    std::array<std::uint8_t, kUuidBytes> bytes{};

    fillWithRandomBytes(bytes);
    bytes[kUuidVersionByte] = (bytes[kUuidVersionByte] & 0x0fU) | 0x40U;
    bytes[kUuidVariantByte] = (bytes[kUuidVariantByte] & 0x3fU) | 0x80U;

    std::string uuid;

    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (std::ranges::find(kUuidHyphenBeforeByte, index) != kUuidHyphenBeforeByte.end()) {
            uuid += '-';
        }

        appendHex(uuid, bytes[index]);
    }

    return uuid;
}

} // namespace react_native_linux
