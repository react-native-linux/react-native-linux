#include "SingleInstanceActivation.h"

namespace react_native_linux {

namespace {

constexpr std::string_view kBusNamePrefix = "org.reactnativelinux.instance.";
constexpr size_t kSkipExecutablePath = 1;

bool isBusNameCharacter(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_' || character == '.';
}

bool isSchemeLeadCharacter(uint8_t byte) { return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z'); }

bool isSchemeTailCharacter(uint8_t byte) {
    return isSchemeLeadCharacter(byte) || (byte >= '0' && byte <= '9') || byte == '+' || byte == '.' || byte == '-';
}

bool looksLikeUri(const ActivationByteArgument& argument) {
    if (argument.empty() || !isSchemeLeadCharacter(argument[0])) {
        return false;
    }

    for (size_t index = 1; index < argument.size(); ++index) {
        if (argument[index] == ':') {
            return true;
        }

        if (!isSchemeTailCharacter(argument[index])) {
            return false;
        }
    }

    return false;
}

} // namespace

std::string singleInstanceBusName(std::string_view applicationId) {
    std::string sanitizedApplicationId;
    sanitizedApplicationId.reserve(applicationId.size());

    for (char character : applicationId) {
        sanitizedApplicationId.push_back(isBusNameCharacter(character) ? character : '_');
    }

    return std::string(kBusNamePrefix) + sanitizedApplicationId;
}

ActivationByteArgument toByteArgument(std::string_view text) {
    return ActivationByteArgument(text.begin(), text.end());
}

std::string fromByteArgument(const ActivationByteArgument& bytes) { return std::string(bytes.begin(), bytes.end()); }

ActivationRequest buildActivationRequest(const std::vector<std::string>& argv, std::string_view workingDirectory) {
    ActivationRequest request;
    request.argv.reserve(argv.size());

    for (const std::string& argument : argv) {
        request.argv.push_back(toByteArgument(argument));
    }

    request.cwd = toByteArgument(workingDirectory);

    return request;
}

std::optional<std::string> extractActivationUrl(const std::vector<ActivationByteArgument>& argv) {
    for (size_t index = kSkipExecutablePath; index < argv.size(); ++index) {
        if (looksLikeUri(argv[index])) {
            return fromByteArgument(argv[index]);
        }
    }

    return std::nullopt;
}

} // namespace react_native_linux
