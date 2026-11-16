#pragma once

#include "Utf16ToUtf8.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace facebook::react {

/**
 * A JavaScript string as the UTF-8 the lexer reads, built from the chunks `jsi::String::getStringData` hands over. A
 * runtime may pass one string as several ASCII and UTF-16 chunks and split a surrogate pair between two of them, so a
 * high surrogate that ends a chunk waits for the next one. Unpaired, it becomes U+FFFD, as with `jsi::String::utf8`.
 */
class SourceText final {
public:
    void append(bool ascii, const void* data, std::size_t length) {
        if (ascii) {
            appendPendingHighSurrogate();
            utf8_.append(static_cast<const char*>(data), length);
            return;
        }

        std::u16string_view units(static_cast<const char16_t*>(data), length);

        if (pendingHighSurrogate_ != 0 && !units.empty() && isLowSurrogate(units.front())) {
            const std::array pair{pendingHighSurrogate_, units.front()};
            margelo::nitro::t3highlights::appendUtf16AsUtf8(utf8_, pair.data(), pair.size());
            pendingHighSurrogate_ = 0;
            units.remove_prefix(1);
        }

        appendPendingHighSurrogate();

        if (!units.empty() && isHighSurrogate(units.back())) {
            pendingHighSurrogate_ = units.back();
            units.remove_suffix(1);
        }

        margelo::nitro::t3highlights::appendUtf16AsUtf8(utf8_, units.data(), units.size());
    }

    [[nodiscard]] std::string finish() && {
        appendPendingHighSurrogate();
        return std::move(utf8_);
    }

private:
    static constexpr bool isHighSurrogate(char16_t unit) { return unit >= 0xD800 && unit <= 0xDBFF; }

    static constexpr bool isLowSurrogate(char16_t unit) { return unit >= 0xDC00 && unit <= 0xDFFF; }

    void appendPendingHighSurrogate() {
        if (pendingHighSurrogate_ != 0) {
            margelo::nitro::t3highlights::appendUtf16AsUtf8(utf8_, &pendingHighSurrogate_, 1);
            pendingHighSurrogate_ = 0;
        }
    }

    std::string utf8_;
    char16_t pendingHighSurrogate_ = 0;
};

} // namespace facebook::react
