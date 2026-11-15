#include "DpopKeys.h"

#include <algorithm>
#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <string_view>

namespace t3_native_controls {
namespace {

constexpr std::size_t kScalarBytes = 32;
constexpr std::size_t kSignatureRepetitions = 32;

/** RFC 6979 A.2.5: the P-256 private scalar and its public point `04 ‖ Ux ‖ Uy`. */
const Bytes kKnownPrivateKey = {0xc9, 0xaf, 0xa9, 0xd8, 0x45, 0xba, 0x75, 0x16, 0x6b, 0x5c, 0x21,
                                0x57, 0x67, 0xb1, 0xd6, 0x93, 0x4e, 0x50, 0xc3, 0xdb, 0x36, 0xe8,
                                0x9b, 0x12, 0x7b, 0x8a, 0x62, 0x2b, 0x12, 0x0f, 0x67, 0x21};
const Bytes kKnownPublicKey = {0x04, 0x60, 0xfe, 0xd4, 0xba, 0x25, 0x5a, 0x9d, 0x31, 0xc9, 0x61, 0xeb, 0x74,
                               0xc6, 0x35, 0x6d, 0x68, 0xc0, 0x49, 0xb8, 0x92, 0x3b, 0x61, 0xfa, 0x6c, 0xe6,
                               0x69, 0x62, 0x2e, 0x60, 0xf2, 0x9f, 0xb6, 0x79, 0x03, 0xfe, 0x10, 0x08, 0xb8,
                               0xbc, 0x99, 0xa4, 0x1a, 0xe9, 0xe9, 0x56, 0x28, 0xbc, 0x64, 0xf2, 0xf1, 0xb2,
                               0x0c, 0x2d, 0x7e, 0x9f, 0x51, 0x77, 0xa3, 0xc2, 0x94, 0xd4, 0x46, 0x22, 0x99};
const Bytes kCurveOrder = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
                           0xff, 0xff, 0xff, 0xff, 0xff, 0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17,
                           0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};

ByteView bytesOf(std::string_view text) { return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()}; }

/** An independent check through libcrypto's verifier, from the public point alone. */
bool verifies(const Bytes& publicKey, ByteView signingInput, const Bytes& rawSignature) {
    std::array<char, 11> group = {"prime256v1"};
    Bytes point = publicKey;
    std::array<OSSL_PARAM, 3> parameters = {
        OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group.data(), 0),
        OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size()),
        OSSL_PARAM_construct_end()};
    const std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
        EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), &EVP_PKEY_CTX_free);
    EVP_PKEY* rawKey = nullptr;

    EXPECT_EQ(EVP_PKEY_fromdata_init(context.get()), 1);
    EXPECT_EQ(EVP_PKEY_fromdata(context.get(), &rawKey, EVP_PKEY_PUBLIC_KEY, parameters.data()), 1);

    const std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(rawKey, &EVP_PKEY_free);
    const std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(ECDSA_SIG_new(), &ECDSA_SIG_free);

    ECDSA_SIG_set0(signature.get(), BN_bin2bn(rawSignature.data(), kScalarBytes, nullptr),
                   BN_bin2bn(rawSignature.data() + kScalarBytes, kScalarBytes, nullptr));

    unsigned char* der = nullptr;
    const int derLength = i2d_ECDSA_SIG(signature.get(), &der);
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(), &EVP_MD_CTX_free);

    EXPECT_EQ(EVP_DigestVerifyInit(digest.get(), nullptr, EVP_sha256(), nullptr, key.get()), 1);

    const bool verified = EVP_DigestVerify(digest.get(), der, derLength, signingInput.data(), signingInput.size()) == 1;

    OPENSSL_free(der);

    return verified;
}

TEST(DpopKeysTest, GeneratesAScalarFollowedByItsUncompressedPublicPoint) {
    const Bytes key = DpopKeys::generate();
    const ByteView privateKey(key.data(), kScalarBytes);
    const Bytes publicKey(key.begin() + kScalarBytes, key.end());

    ASSERT_EQ(key.size(), 97U);
    EXPECT_EQ(publicKey.front(), 0x04);
    EXPECT_TRUE(DpopKeys::matches(privateKey, publicKey));
    EXPECT_NE(DpopKeys::generate(), key);
}

TEST(DpopKeysTest, DerivesTheKnownPublicPointOfAKnownScalar) {
    EXPECT_TRUE(DpopKeys::matches(kKnownPrivateKey, kKnownPublicKey));
}

TEST(DpopKeysTest, RejectsAPublicPointOfAnotherKeyOrLength) {
    Bytes otherPoint = kKnownPublicKey;

    otherPoint.back() ^= 0x01;

    EXPECT_FALSE(DpopKeys::matches(kKnownPrivateKey, otherPoint));
    EXPECT_FALSE(DpopKeys::matches(kKnownPrivateKey, ByteView(kKnownPublicKey).first(64)));
}

TEST(DpopKeysTest, SignsRawSignaturesThatVerifyAgainstThePublicPoint) {
    const ByteView signingInput = bytesOf("header.payload");

    for (std::size_t repetition = 0; repetition < kSignatureRepetitions; ++repetition) {
        const Bytes signature = DpopKeys::sign(kKnownPrivateKey, signingInput);

        ASSERT_EQ(signature.size(), 64U);
        EXPECT_TRUE(verifies(kKnownPublicKey, signingInput, signature));
        EXPECT_FALSE(verifies(kKnownPublicKey, bytesOf("header.payloae"), signature));
    }
}

TEST(DpopKeysTest, SignsAnEmptySigningInput) {
    EXPECT_TRUE(verifies(kKnownPublicKey, {}, DpopKeys::sign(kKnownPrivateKey, {})));
}

TEST(DpopKeysTest, RefusesAPrivateKeyThatIsNotAP256Scalar) {
    const Bytes zero(kScalarBytes, 0);
    Bytes belowOrder = kCurveOrder;

    belowOrder.back() -= 1;

    for (const Bytes& privateKey :
         {Bytes(kScalarBytes - 1, 1), Bytes(kScalarBytes + 1, 1), zero, kCurveOrder, Bytes(kScalarBytes, 0xff)}) {
        EXPECT_THROW(DpopKeys::matches(privateKey, kKnownPublicKey), std::invalid_argument);
        EXPECT_THROW(DpopKeys::sign(privateKey, bytesOf("input")), std::invalid_argument);
    }

    EXPECT_EQ(DpopKeys::sign(belowOrder, bytesOf("input")).size(), 64U);
}

} // namespace
} // namespace t3_native_controls
