#pragma once

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unicode/ucol.h>
#include <unicode/udat.h>
#include <unicode/unumberformatter.h>
#include <utility>
#include <variant>
#include <vector>

namespace react_native_linux {

/** What JavaScript sees as a `RangeError`: a structurally invalid locale or option value, or an unsupported option. */
struct IntlRangeError final : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

/** What JavaScript sees as a `TypeError`: options that contradict each other, as ECMA-402 defines it. */
struct IntlTypeError final : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

/** How ECMA-402's `GetOption` coerces an option before it is read: `String`, `Boolean` or `Number`. */
enum class IntlOptionType { String, Boolean, Number };

/** An option ECMA-402 defines for a constructor; one this platform does not implement throws when it is passed. */
struct IntlOptionName {
    std::string_view name;
    IntlOptionType type;
    bool isSupported;
};

using IntlOptionValue = std::variant<std::string, bool, double>;

/** The options a caller passed, already coerced to their `IntlOptionType`; options left `undefined` are absent. */
using IntlOptions = std::vector<std::pair<std::string_view, IntlOptionValue>>;

/** What `resolvedOptions()` returns, in the property order ECMA-402 lists. */
using IntlResolvedOptions = std::vector<std::pair<std::string_view, IntlOptionValue>>;

/** What `formatToParts` returns: each part's ECMA-402 type (`hour`, `literal`, `timeZoneName`) and its text. */
using IntlParts = std::vector<std::pair<std::string_view, std::string>>;

/** ECMA-402's `CanonicalizeLocaleList` over tags already read from JavaScript: canonical, deduplicated, in order. */
std::vector<std::string> canonicalizeLocaleList(std::span<const std::string> tags);

/**
 * The locale an `Intl` object made with no locale uses, from a POSIX locale name as `localeFromEnvironment` gives
 * it (`de_DE`). An unset, `C` or `POSIX` locale, or one ICU has no data for, is `en-US`.
 */
std::string defaultIntlLocale(std::string_view posixLocale);

/** The host's IANA time zone as ICU detects it, `UTC` when it cannot. */
std::string defaultIntlTimeZone();

/**
 * `Intl.DateTimeFormat` over `udat_`: the component options, `dateStyle` and `timeStyle`, `hourCycle`, `hour12`,
 * `timeZone` and `timeZoneName`, and the locale's `-u-hc`. Every other option ECMA-402 defines is a
 * `IntlRangeError` naming it.
 *
 * Threading contract: an instance is used by one thread at a time; the JavaScript thread owns the ones it makes.
 */
class IntlDateTimeFormat final {
public:
    static std::span<const IntlOptionName> optionNames() noexcept;

    IntlDateTimeFormat(std::span<const std::string> requestedLocales, const IntlOptions& options,
                       std::string_view defaultLocale, std::string_view defaultTimeZone);

    std::string format(double epochMilliseconds) const;
    IntlParts formatToParts(double epochMilliseconds) const;
    const IntlResolvedOptions& resolvedOptions() const noexcept;

private:
    std::unique_ptr<UDateFormat, decltype(&udat_close)> format_;
    IntlResolvedOptions resolvedOptions_;
};

/**
 * `Intl.NumberFormat` over `unumf_` skeletons: the `decimal` and `currency` styles, `currency`, and the fraction
 * digit options. Every other option ECMA-402 defines is a `IntlRangeError` naming it.
 *
 * Threading contract: as `IntlDateTimeFormat`'s.
 */
class IntlNumberFormat final {
public:
    static std::span<const IntlOptionName> optionNames() noexcept;

    IntlNumberFormat(std::span<const std::string> requestedLocales, const IntlOptions& options,
                     std::string_view defaultLocale);

    std::string format(double value) const;
    const IntlResolvedOptions& resolvedOptions() const noexcept;

private:
    std::unique_ptr<UNumberFormatter, decltype(&unumf_close)> formatter_;
    std::unique_ptr<UFormattedNumber, decltype(&unumf_closeResult)> result_;
    IntlResolvedOptions resolvedOptions_;
};

/**
 * `Intl.Collator` over `ucol_`: `usage: "sort"`, `sensitivity`, `numeric` and the locale's `-u-kn`. Every other
 * option ECMA-402 defines is a `IntlRangeError` naming it.
 *
 * Threading contract: as `IntlDateTimeFormat`'s.
 */
class IntlCollator final {
public:
    static std::span<const IntlOptionName> optionNames() noexcept;

    IntlCollator(std::span<const std::string> requestedLocales, const IntlOptions& options,
                 std::string_view defaultLocale);

    int compare(std::string_view left, std::string_view right) const;
    const IntlResolvedOptions& resolvedOptions() const noexcept;

private:
    std::unique_ptr<UCollator, decltype(&ucol_close)> collator_;
    IntlResolvedOptions resolvedOptions_;
};

} // namespace react_native_linux
