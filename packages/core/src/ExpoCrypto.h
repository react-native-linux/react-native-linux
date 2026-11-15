#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * expo-crypto's primitives (#276) over OpenSSL's libcrypto, as its iOS and Android modules compute them. Every
 * function is pure apart from the randomness and is thread-safe.
 */

/**
 * The digest of `data` under one of expo-crypto's `CryptoDigestAlgorithm` values that T3 Code uses: `MD5`, `SHA-1`,
 * `SHA-256`, `SHA-384` and `SHA-512`. `MD2` and `MD4` live only in OpenSSL's legacy provider, so they throw
 * `std::invalid_argument`, as any other name does.
 */
[[nodiscard]] std::vector<std::uint8_t> cryptoDigest(std::string_view algorithm, std::span<const std::uint8_t> data);

/** `cryptoDigest` encoded as `encoding`: `hex` in lowercase, or `base64` padded and unwrapped. */
[[nodiscard]] std::string encodedCryptoDigest(std::string_view algorithm, std::string_view data,
                                              std::string_view encoding);

/** Fills `bytes` from the operating system's CSPRNG through `RAND_bytes`. */
void fillWithRandomBytes(std::span<std::uint8_t> bytes);

/** An RFC 9562 version 4 UUID in lowercase, as iOS's `UUID().uuidString.lowercased()` and Android's answer it. */
[[nodiscard]] std::string randomUuid();

} // namespace react_native_linux
