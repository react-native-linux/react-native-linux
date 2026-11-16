#include "ExpoSecureStore.h"

#include "ExpoCrypto.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::size_t kNonceBytes = 12;
constexpr std::size_t kTagBytes = 16;
constexpr char kKeySalt[] = "react-native-linux expo-secure-store";

void require(bool succeeded, const std::string& what) {
    if (!succeeded) {
        throw std::runtime_error(what);
    }
}

void requireOpenSsl(int result, const char* what) { require(result == 1, std::string("OpenSSL: ") + what); }

void requireSystem(bool succeeded, const std::string& what, const std::filesystem::path& path) {
    const int error = errno;

    require(succeeded, what + " " + path.string() + ": " + std::error_code(error, std::generic_category()).message());
}

struct CipherContextFree {
    void operator()(EVP_CIPHER_CTX* context) const noexcept { EVP_CIPHER_CTX_free(context); }
};

struct KdfFree {
    void operator()(EVP_KDF* kdf) const noexcept { EVP_KDF_free(kdf); }
};

struct KdfContextFree {
    void operator()(EVP_KDF_CTX* context) const noexcept { EVP_KDF_CTX_free(context); }
};

/** Closes a descriptor that is known to be open. */
struct OpenDescriptor {
    explicit OpenDescriptor(int descriptor) : descriptor(descriptor) {}

    OpenDescriptor(const OpenDescriptor&) = delete;
    OpenDescriptor(OpenDescriptor&&) = delete;
    OpenDescriptor& operator=(const OpenDescriptor&) = delete;
    OpenDescriptor& operator=(OpenDescriptor&&) = delete;

    ~OpenDescriptor() { ::close(descriptor); }

    int descriptor;
};

std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);

    requireSystem(file.is_open(), "cannot read", path);

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeAtomically(const std::filesystem::path& path, std::string_view contents) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    requireSystem(descriptor >= 0, "cannot create", temporary);
    {
        const OpenDescriptor closer{descriptor};

        requireSystem(::write(descriptor, contents.data(), contents.size()) == static_cast<ssize_t>(contents.size()),
                      "cannot write", temporary);
        requireSystem(::fsync(descriptor) == 0, "cannot sync", temporary);
    }
    std::filesystem::rename(temporary, path);
}

std::string associatedData(const SecretLocation& location) { return location.service + '\0' + location.key; }

std::string seal(std::span<const std::uint8_t> key, const std::string& associated, const std::string& plaintext) {
    std::string sealed(kNonceBytes + plaintext.size() + kTagBytes, '\0');
    auto* const nonce = reinterpret_cast<std::uint8_t*>(sealed.data());
    const std::unique_ptr<EVP_CIPHER_CTX, CipherContextFree> context(EVP_CIPHER_CTX_new());
    int length = 0;

    fillWithRandomBytes(std::span(nonce, kNonceBytes));
    requireOpenSsl(EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce), "seal init");
    requireOpenSsl(EVP_EncryptUpdate(context.get(), nullptr, &length,
                                     reinterpret_cast<const std::uint8_t*>(associated.data()),
                                     static_cast<int>(associated.size())),
                   "seal associated data");
    requireOpenSsl(EVP_EncryptUpdate(context.get(), nonce + kNonceBytes, &length,
                                     reinterpret_cast<const std::uint8_t*>(plaintext.data()),
                                     static_cast<int>(plaintext.size())),
                   "seal");
    requireOpenSsl(EVP_EncryptFinal_ex(context.get(), nonce + kNonceBytes + length, &length), "seal final");
    requireOpenSsl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, static_cast<int>(kTagBytes),
                                       nonce + kNonceBytes + plaintext.size()),
                   "seal tag");

    return sealed;
}

std::string unseal(std::span<const std::uint8_t> key, const std::string& associated, std::string sealed) {
    require(sealed.size() >= kNonceBytes + kTagBytes, "the file is too short to be a sealed value");

    const std::size_t ciphertextBytes = sealed.size() - kNonceBytes - kTagBytes;
    auto* const nonce = reinterpret_cast<std::uint8_t*>(sealed.data());
    const std::unique_ptr<EVP_CIPHER_CTX, CipherContextFree> context(EVP_CIPHER_CTX_new());
    std::string plaintext(ciphertextBytes, '\0');
    int length = 0;

    requireOpenSsl(EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce), "open init");
    requireOpenSsl(EVP_DecryptUpdate(context.get(), nullptr, &length,
                                     reinterpret_cast<const std::uint8_t*>(associated.data()),
                                     static_cast<int>(associated.size())),
                   "open associated data");
    requireOpenSsl(EVP_DecryptUpdate(context.get(), reinterpret_cast<std::uint8_t*>(plaintext.data()), &length,
                                     nonce + kNonceBytes, static_cast<int>(ciphertextBytes)),
                   "open");
    requireOpenSsl(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, static_cast<int>(kTagBytes),
                                       nonce + kNonceBytes + ciphertextBytes),
                   "open tag");
    requireOpenSsl(
        EVP_DecryptFinal_ex(context.get(), reinterpret_cast<std::uint8_t*>(plaintext.data()) + length, &length),
        "the value does not authenticate under this machine, user and application");

    return plaintext;
}

std::string hexOf(std::span<const std::uint8_t> bytes) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;

    for (const std::uint8_t byte : bytes) {
        hex.push_back(kDigits[byte >> 4U]);
        hex.push_back(kDigits[byte & 0x0FU]);
    }

    return hex;
}

} // namespace

EncryptedFileBackend::EncryptedFileBackend(std::filesystem::path directory,
                                           const std::filesystem::path& machineIdentifierPath,
                                           const std::string& applicationIdentifier)
    : directory_(std::move(directory)) {
    std::string machineIdentifier = readFile(machineIdentifierPath);

    while (!machineIdentifier.empty() && machineIdentifier.back() == '\n') {
        machineIdentifier.pop_back();
    }

    require(!machineIdentifier.empty(), machineIdentifierPath.string() + " is empty");

    std::string info = std::to_string(::getuid()) + '\0' + applicationIdentifier;
    const std::unique_ptr<EVP_KDF, KdfFree> kdf(EVP_KDF_fetch(nullptr, OSSL_KDF_NAME_HKDF, nullptr));
    const std::unique_ptr<EVP_KDF_CTX, KdfContextFree> context(EVP_KDF_CTX_new(kdf.get()));
    std::string digest = "SHA256";
    std::string salt = kKeySalt;
    const OSSL_PARAM parameters[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest.data(), 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, machineIdentifier.data(), machineIdentifier.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt.data(), salt.size()),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, info.data(), info.size()),
        OSSL_PARAM_construct_end(),
    };
    const int derived = EVP_KDF_derive(context.get(), key_.data(), key_.size(), parameters);

    OPENSSL_cleanse(machineIdentifier.data(), machineIdentifier.size());
    requireOpenSsl(derived, "derive the key");
}

EncryptedFileBackend::~EncryptedFileBackend() { OPENSSL_cleanse(key_.data(), key_.size()); }

std::optional<std::string> EncryptedFileBackend::read(const SecretLocation& location) {
    const std::filesystem::path path = pathOf(location);

    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }

    return unseal(key_, associatedData(location), readFile(path));
}

void EncryptedFileBackend::write(const SecretLocation& location, const std::string& value) {
    std::filesystem::create_directories(directory_);
    std::filesystem::permissions(directory_, std::filesystem::perms::owner_all);
    writeAtomically(pathOf(location), seal(key_, associatedData(location), value));
}

void EncryptedFileBackend::remove(const SecretLocation& location) { std::filesystem::remove(pathOf(location)); }

std::filesystem::path EncryptedFileBackend::pathOf(const SecretLocation& location) const {
    const std::string name = associatedData(location);

    return directory_ /
           hexOf(cryptoDigest("SHA-256", std::span(reinterpret_cast<const std::uint8_t*>(name.data()), name.size())));
}

void SecureStore::configure(std::filesystem::path applicationDirectory, KeyringConnector connectKeyring,
                            std::filesystem::path machineIdentifierPath) {
    applicationDirectory_ = std::move(applicationDirectory);
    connectKeyring_ = std::move(connectKeyring);
    machineIdentifierPath_ = std::move(machineIdentifierPath);
}

void SecureStore::post(Task task) {
    worker_.post([this, task = std::move(task)] { task(*this); });
}

SecretBackend& SecureStore::backend() {
    if (backend_ != nullptr) {
        return *backend_;
    }

    require(!applicationDirectory_.empty(), "this host has no application directory");

    const std::string applicationIdentifier = applicationDirectory_.filename().string();

    backend_ = connectKeyring_(applicationIdentifier);

    if (backend_ == nullptr) {
        const std::filesystem::path directory = applicationDirectory_ / "SecureStore";

        backend_ = std::make_unique<EncryptedFileBackend>(directory, machineIdentifierPath_, applicationIdentifier);
        std::cerr << "[rnl-secure-store] no Secret Service on the session bus: values are stored encrypted under "
                  << directory.string()
                  << ", which protects them against copying, not against code running as this user" << std::endl;
    }

    return *backend_;
}

} // namespace react_native_linux
