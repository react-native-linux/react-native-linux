#include "StartupActivationToken.h"

namespace react_native_linux {

StartupActivationToken StartupActivationToken::fromEnvironment(const std::optional<std::string>& envValue) {
    if (!envValue.has_value() || envValue->empty()) {
        return StartupActivationToken(std::nullopt);
    }

    return StartupActivationToken(envValue);
}

StartupActivationToken::StartupActivationToken(std::optional<std::string> token) : token_(std::move(token)) {}

std::optional<std::string> StartupActivationToken::take() {
    std::optional<std::string> taken = std::move(token_);
    token_.reset();

    return taken;
}

bool StartupActivationToken::consumed() const noexcept { return !token_.has_value(); }

} // namespace react_native_linux
