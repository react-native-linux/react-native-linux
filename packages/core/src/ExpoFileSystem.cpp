#include "ExpoFileSystem.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>

namespace react_native_linux {

namespace {

constexpr std::string_view kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::string_view kUnencodedPathCharacters = "-._~/!$&'()*+,;=:@";
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr int kBitsPerBase64Digit = 6;
constexpr int kBitsPerByte = 8;
constexpr unsigned kBase64DigitMask = 0x3F;
constexpr unsigned kByteMask = 0xFF;
constexpr unsigned kNibbleBits = 4;
constexpr unsigned kNibbleMask = 0xF;
constexpr size_t kBase64QuantumLength = 4;
constexpr int kNotAHexDigit = -1;
constexpr int kDecimalDigitCount = 10;

std::string describe(const std::filesystem::path& path) { return "'" + path.string() + "'"; }

int hexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }

    if (character >= 'A' && character <= 'F') {
        return character - 'A' + kDecimalDigitCount;
    }

    if (character >= 'a' && character <= 'f') {
        return character - 'a' + kDecimalDigitCount;
    }

    return kNotAHexDigit;
}

std::string percentDecode(std::string_view text) {
    std::string decoded;

    for (size_t index = 0; index < text.size(); ++index) {
        const bool escaped = text[index] == '%' && index + 2 < text.size() &&
                             hexValue(text[index + 1]) != kNotAHexDigit && hexValue(text[index + 2]) != kNotAHexDigit;

        if (escaped) {
            decoded.push_back(
                static_cast<char>((hexValue(text[index + 1]) << kNibbleBits) | hexValue(text[index + 2])));
            index += 2;
        } else {
            decoded.push_back(text[index]);
        }
    }

    return decoded;
}

} // namespace

FileSystemDirectories fileSystemDirectories(const std::filesystem::path& applicationDirectory, const char* cacheHome,
                                            const std::filesystem::path& home) {
    // The XDG Base Directory rule: an unset, empty or relative value means the default.
    const std::filesystem::path configuredCacheHome(cacheHome == nullptr ? "" : cacheHome);
    const std::filesystem::path cacheDirectory =
        configuredCacheHome.is_absolute() ? configuredCacheHome : home / ".cache";

    return {.document = applicationDirectory / "Documents", .cache = cacheDirectory / applicationDirectory.filename()};
}

std::filesystem::path pathFromFileUri(std::string_view uri) {
    std::string_view rest = uri;

    if (rest.starts_with("file:")) {
        rest.remove_prefix(std::string_view("file:").size());

        if (rest.starts_with("//")) {
            rest.remove_prefix(2);
        }
    }

    if (!rest.starts_with('/')) {
        throw std::invalid_argument("'" + std::string(uri) + "' is not a file URI or an absolute path");
    }

    std::filesystem::path path = std::filesystem::path(percentDecode(rest)).lexically_normal();

    return path.has_filename() || path == path.root_path() ? path : path.parent_path();
}

std::string fileUriFromPath(const std::filesystem::path& path, FileSystemEntryKind kind) {
    std::string uri = "file://";

    for (const char character : path.string()) {
        const auto byte = static_cast<unsigned char>(character);

        if (std::isalnum(byte) != 0 || kUnencodedPathCharacters.find(character) != std::string_view::npos) {
            uri.push_back(character);
        } else {
            uri.push_back('%');
            uri.push_back(kHexDigits[byte >> kNibbleBits]);
            uri.push_back(kHexDigits[byte & kNibbleMask]);
        }
    }

    if (kind == FileSystemEntryKind::Directory && !uri.ends_with('/')) {
        uri.push_back('/');
    }

    return uri;
}

std::string encodeBase64(std::string_view bytes) {
    std::string encoded;
    unsigned buffer = 0;
    int bufferedBits = 0;

    for (const char character : bytes) {
        buffer = (buffer << kBitsPerByte) | static_cast<unsigned char>(character);
        bufferedBits += kBitsPerByte;

        while (bufferedBits >= kBitsPerBase64Digit) {
            bufferedBits -= kBitsPerBase64Digit;
            encoded.push_back(kBase64Alphabet[(buffer >> bufferedBits) & kBase64DigitMask]);
        }
    }

    if (bufferedBits > 0) {
        encoded.push_back(kBase64Alphabet[(buffer << (kBitsPerBase64Digit - bufferedBits)) & kBase64DigitMask]);
    }

    encoded.append((kBase64QuantumLength - encoded.size() % kBase64QuantumLength) % kBase64QuantumLength, '=');

    return encoded;
}

std::string decodeBase64(std::string_view text) {
    const std::string_view digits = text.substr(0, text.find_last_not_of('=') + 1);
    const size_t padding = text.size() - digits.size();
    std::string decoded;
    unsigned buffer = 0;
    int bufferedBits = 0;

    if (text.size() % kBase64QuantumLength != 0 || padding > 2) {
        throw std::invalid_argument("the text is not padded base64");
    }

    for (const char character : digits) {
        const size_t value = kBase64Alphabet.find(character);

        if (value == std::string_view::npos) {
            throw std::invalid_argument("the text is not padded base64");
        }

        buffer = (buffer << kBitsPerBase64Digit) | static_cast<unsigned>(value);
        bufferedBits += kBitsPerBase64Digit;

        if (bufferedBits >= kBitsPerByte) {
            bufferedBits -= kBitsPerByte;
            decoded.push_back(static_cast<char>((buffer >> bufferedBits) & kByteMask));
        }
    }

    return decoded;
}

void validateEntryPath(const std::filesystem::path& path, FileSystemEntryKind kind) {
    if (entryExists(path,
                    kind == FileSystemEntryKind::File ? FileSystemEntryKind::Directory : FileSystemEntryKind::File)) {
        throw std::invalid_argument(describe(path) + (kind == FileSystemEntryKind::File
                                                          ? " is a directory, not a file"
                                                          : " is a file, not a directory"));
    }
}

bool entryExists(const std::filesystem::path& path, FileSystemEntryKind kind) {
    return kind == FileSystemEntryKind::File ? std::filesystem::is_regular_file(path)
                                             : std::filesystem::is_directory(path);
}

std::optional<std::uintmax_t> fileSize(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }

    return std::filesystem::file_size(path);
}

void createFile(const std::filesystem::path& path, bool intermediates, bool overwrite) {
    if (!overwrite && std::filesystem::exists(path)) {
        throw std::runtime_error("cannot create " + describe(path) + ": it already exists");
    }

    if (intermediates) {
        std::filesystem::create_directories(path.parent_path());
    }

    writeFileBytes(path, "", false);
}

void createDirectory(const std::filesystem::path& path, bool intermediates, bool overwrite, bool idempotent) {
    if (idempotent && std::filesystem::is_directory(path)) {
        return;
    }

    if (std::filesystem::exists(path)) {
        if (!overwrite) {
            throw std::runtime_error("cannot create " + describe(path) + ": it already exists");
        }

        std::filesystem::remove_all(path);
    }

    if (intermediates) {
        std::filesystem::create_directories(path);
    } else {
        std::filesystem::create_directory(path);
    }
}

void removeEntry(const std::filesystem::path& path) {
    if (std::filesystem::remove_all(path) == 0) {
        throw std::runtime_error("cannot delete " + describe(path) + ": it does not exist");
    }
}

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);

    if (!stream.is_open() || !std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("cannot read " + describe(path) + ": it is not a readable file");
    }

    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void writeFileBytes(const std::filesystem::path& path, std::string_view bytes, bool append) {
    std::ofstream stream(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));

    if (!stream.is_open()) {
        throw std::runtime_error("cannot write " + describe(path) + ": it cannot be opened for writing");
    }

    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::filesystem::path relocateEntry(const std::filesystem::path& source, FileSystemEntryKind sourceKind,
                                    const std::filesystem::path& destination, FileSystemEntryKind destinationKind,
                                    bool overwrite, bool move) {
    const std::filesystem::path target =
        destinationKind == FileSystemEntryKind::Directory ? destination / source.filename() : destination;

    if (!entryExists(source, sourceKind)) {
        throw std::runtime_error("cannot relocate " + describe(source) + ": it does not exist");
    }

    if (sourceKind == FileSystemEntryKind::Directory && destinationKind == FileSystemEntryKind::File) {
        throw std::invalid_argument("cannot relocate the directory " + describe(source) + " onto a file");
    }

    if (std::filesystem::exists(target)) {
        if (!overwrite) {
            throw std::runtime_error("cannot relocate " + describe(source) + ": " + describe(target) +
                                     " already exists");
        }

        std::filesystem::remove_all(target);
    }

    if (move) {
        std::filesystem::rename(source, target);
    } else {
        std::filesystem::copy(source, target, std::filesystem::copy_options::recursive);
    }

    return target;
}

std::vector<FileSystemRecord> listDirectory(const std::filesystem::path& path) {
    std::vector<FileSystemRecord> records;

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(path)) {
        records.push_back({.isDirectory = entry.is_directory(), .path = entry.path()});
    }

    std::ranges::sort(records, {}, &FileSystemRecord::path);

    return records;
}

} // namespace react_native_linux
