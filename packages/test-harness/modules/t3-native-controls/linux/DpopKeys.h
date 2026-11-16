#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace t3_native_controls {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

/**
 * P-256 keys for DPoP proofs, over OpenSSL's libcrypto, in the byte layout the iOS and Android implementations use:
 * a 32-byte big-endian private scalar and a 65-byte uncompressed public point. Every function is pure and
 * thread-safe, and throws `std::invalid_argument` for a key that is not P-256.
 */
class DpopKeys {
public:
    /** A new private scalar followed by its public point: 97 bytes. */
    static Bytes generate();

    static bool matches(ByteView privateKey, ByteView publicKey);

    /** ES256 over the JWS signing input, as the raw 64-byte `r‖s` pair JWS uses. */
    static Bytes sign(ByteView privateKey, ByteView signingInput);
};

} // namespace t3_native_controls
