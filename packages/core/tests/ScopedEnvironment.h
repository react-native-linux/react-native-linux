#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace react_native_linux {

/** Sets an environment variable, or unsets it for `nullptr`, and puts back what the process had when it ends. */
class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const char* value) : name_(name) {
        const char* const previous = std::getenv(name);

        if (previous != nullptr) {
            previous_ = previous;
        }

        if (value == nullptr) {
            unsetenv(name);
        } else {
            setenv(name, value, 1);
        }
    }

    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment(ScopedEnvironment&&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(ScopedEnvironment&&) = delete;

    ~ScopedEnvironment() {
        if (previous_.has_value()) {
            setenv(name_, previous_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> previous_;
};

} // namespace react_native_linux
