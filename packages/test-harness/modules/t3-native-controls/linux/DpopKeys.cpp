#include "DpopKeys.h"

#include <algorithm>
#include <array>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <stdexcept>

namespace t3_native_controls {
namespace {

constexpr std::size_t kScalarBytes = 32;
constexpr std::size_t kPublicPointBytes = 1 + 2 * kScalarBytes;
constexpr std::size_t kMaximumDerSignatureBytes = 72;

constexpr std::array<std::uint8_t, kScalarBytes> kCurveOrder = {
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};

/**
 * RFC 5915's `ECPrivateKey` for prime256v1 without its optional public key, up to the 32 scalar bytes that follow
 * it. libcrypto derives the public point while decoding this, which no OSSL_PARAM import does.
 */
constexpr std::array<std::uint8_t, 7> kPrivateKeyPrefix = {0x30, 0x31, 0x02, 0x01, 0x01, 0x04, 0x20};
constexpr std::array<std::uint8_t, 12> kPrivateKeySuffix = {0xa0, 0x0a, 0x06, 0x08, 0x2a, 0x86,
                                                            0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};

struct LibcryptoFree {
    void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }

    void operator()(EVP_MD_CTX* digest) const { EVP_MD_CTX_free(digest); }

    void operator()(ECDSA_SIG* signature) const { ECDSA_SIG_free(signature); }

    void operator()(BIGNUM* number) const { BN_clear_free(number); }
};

template <typename T> using Owned = std::unique_ptr<T, LibcryptoFree>;

void ensure(bool succeeded) {
    if (!succeeded) // COV_EXCL: libcrypto fails these calls only on an allocation failure, which no input reaches
        throw std::runtime_error("libcrypto failed a DPoP key operation"); // COV_EXCL: see the line above
}

template <typename T> Owned<T> owned(T* pointer) {
    ensure(pointer != nullptr);

    return Owned<T>(pointer);
}

void requireScalar(ByteView privateKey) {
    const bool isScalar = privateKey.size() == kScalarBytes &&
                          std::ranges::any_of(privateKey, [](std::uint8_t byte) { return byte != 0; }) &&
                          std::ranges::lexicographical_compare(privateKey, kCurveOrder);

    if (!isScalar) {
        throw std::invalid_argument("A DPoP private key is a 32-byte P-256 scalar.");
    }
}

Owned<EVP_PKEY> keyFromScalar(ByteView privateKey) {
    requireScalar(privateKey);

    std::array<std::uint8_t, kPrivateKeyPrefix.size() + kScalarBytes + kPrivateKeySuffix.size()> encoded{};
    auto cursor = std::ranges::copy(kPrivateKeyPrefix, encoded.begin()).out;

    cursor = std::ranges::copy(privateKey, cursor).out;
    std::ranges::copy(kPrivateKeySuffix, cursor);

    const unsigned char* input = encoded.data();
    EVP_PKEY* key = d2i_PrivateKey(EVP_PKEY_EC, nullptr, &input, static_cast<long>(encoded.size()));

    OPENSSL_cleanse(encoded.data(), encoded.size());

    return owned(key);
}

/** The uncompressed point, asked for by name: OpenSSL 3.0.0 to 3.0.7 export `OSSL_PKEY_PARAM_PUB_KEY` compressed. */
Bytes publicPoint(EVP_PKEY& key) {
    Bytes point(kPublicPointBytes);
    std::size_t length = 0;

    ensure(EVP_PKEY_set_utf8_string_param(&key, OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT,
                                          OSSL_PKEY_EC_POINT_CONVERSION_FORMAT_UNCOMPRESSED) == 1);
    ensure(EVP_PKEY_get_octet_string_param(&key, OSSL_PKEY_PARAM_ENCODED_PUBLIC_KEY, point.data(), point.size(),
                                           &length) == 1);
    ensure(length == kPublicPointBytes);

    return point;
}

void appendScalar(Bytes& output, const BIGNUM& number) {
    output.resize(output.size() + kScalarBytes);
    ensure(BN_bn2binpad(&number, output.data() + output.size() - kScalarBytes, kScalarBytes) ==
           static_cast<int>(kScalarBytes));
}

} // namespace

Bytes DpopKeys::generate() {
    const Owned<EVP_PKEY> key = owned(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"));
    BIGNUM* scalar = nullptr;

    ensure(EVP_PKEY_get_bn_param(key.get(), OSSL_PKEY_PARAM_PRIV_KEY, &scalar) == 1);

    const Owned<BIGNUM> ownedScalar = owned(scalar);
    const Bytes point = publicPoint(*key);
    Bytes output(kScalarBytes + kPublicPointBytes);

    std::ranges::copy(point, output.begin() + kScalarBytes);
    ensure(BN_bn2binpad(ownedScalar.get(), output.data(), kScalarBytes) == static_cast<int>(kScalarBytes));

    return output;
}

bool DpopKeys::matches(ByteView privateKey, ByteView publicKey) {
    return std::ranges::equal(publicPoint(*keyFromScalar(privateKey)), publicKey);
}

Bytes DpopKeys::sign(ByteView privateKey, ByteView signingInput) {
    const Owned<EVP_PKEY> key = keyFromScalar(privateKey);
    const Owned<EVP_MD_CTX> digest = owned(EVP_MD_CTX_new());
    std::array<unsigned char, kMaximumDerSignatureBytes> der{};
    std::size_t derLength = der.size();

    ensure(EVP_DigestSignInit(digest.get(), nullptr, EVP_sha256(), nullptr, key.get()) == 1);
    ensure(EVP_DigestSign(digest.get(), der.data(), &derLength, signingInput.data(), signingInput.size()) == 1);

    const unsigned char* input = der.data();
    const Owned<ECDSA_SIG> signature = owned(d2i_ECDSA_SIG(nullptr, &input, static_cast<long>(derLength)));
    Bytes output;

    appendScalar(output, *ECDSA_SIG_get0_r(signature.get()));
    appendScalar(output, *ECDSA_SIG_get0_s(signature.get()));

    return output;
}

} // namespace t3_native_controls
