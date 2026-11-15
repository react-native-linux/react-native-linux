#include "Intl.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <unicode/ucal.h>
#include <unicode/ucurr.h>
#include <unicode/udatpg.h>
#include <unicode/uenum.h>
#include <unicode/ufieldpositer.h>
#include <unicode/uloc.h>
#include <unicode/unumsys.h>
#include <unicode/ustring.h>

namespace react_native_linux {

namespace {

constexpr std::string_view kFallbackLocale = "en-US";
constexpr std::string_view kInvalidLocale = "Incorrect locale information provided";
constexpr double kMaximumTimeValue = 8.64e15;
constexpr UChar32 kReplacementCharacter = 0xFFFD;
constexpr int kMaximumFractionDigits = 100;

struct Choice {
    std::string_view value;
    std::string_view symbol;
};

void requireSuccess(UErrorCode status, std::string_view message) {
    if (U_FAILURE(status)) {
        throw IntlRangeError(std::string(message));
    }
}

/** ICU's preflight convention: ask for the length with no buffer, then fill a buffer of exactly that length. */
template <typename Character, typename Read>
std::basic_string<Character> readIcuString(std::string_view failureMessage, const Read& read) {
    UErrorCode status = U_ZERO_ERROR;
    const int32_t length = read(nullptr, 0, &status);
    std::basic_string<Character> text(static_cast<std::size_t>(length), Character{});

    status = U_ZERO_ERROR;
    read(text.data(), length, &status);
    requireSuccess(status, failureMessage);

    return text;
}

std::u16string toUtf16(std::string_view text) {
    return readIcuString<char16_t>("Invalid UTF-8", [text](UChar* buffer, int32_t capacity, UErrorCode* status) {
        int32_t length = 0;

        u_strFromUTF8WithSub(buffer, capacity, &length, text.data(), static_cast<int32_t>(text.size()),
                             kReplacementCharacter, nullptr, status);

        return length;
    });
}

std::string toUtf8(std::u16string_view text) {
    return readIcuString<char>("Invalid UTF-16", [text](char* buffer, int32_t capacity, UErrorCode* status) {
        int32_t length = 0;

        u_strToUTF8WithSub(buffer, capacity, &length, text.data(), static_cast<int32_t>(text.size()),
                           kReplacementCharacter, nullptr, status);

        return length;
    });
}

std::string localeIdFromTag(const std::string& tag) {
    int32_t parsedLength = 0;
    std::string localeId =
        readIcuString<char>(kInvalidLocale, [&tag, &parsedLength](char* buffer, int32_t capacity, UErrorCode* status) {
            return uloc_forLanguageTag(tag.c_str(), buffer, capacity, &parsedLength, status);
        });

    if (tag.empty() || parsedLength != static_cast<int32_t>(tag.size())) {
        throw IntlRangeError(std::string(kInvalidLocale));
    }

    return localeId;
}

std::string canonicalTag(const std::string& tag) {
    const std::string localeId = localeIdFromTag(tag);

    return readIcuString<char>(kInvalidLocale, [&localeId](char* buffer, int32_t capacity, UErrorCode* status) {
        return uloc_toLanguageTag(localeId.c_str(), buffer, capacity, true, status);
    });
}

/** With the legacy aliases, such as `zh_TW` and `sr_RS`, which ICU's plain list leaves out though it has data. */
bool isAvailable(const std::string& localeId) {
    static const std::set<std::string, std::less<>> availableLocaleIds = [] {
        UErrorCode status = U_ZERO_ERROR;
        const std::unique_ptr<UEnumeration, decltype(&uenum_close)> available(
            uloc_openAvailableByType(ULOC_AVAILABLE_WITH_LEGACY_ALIASES, &status), &uenum_close);
        std::set<std::string, std::less<>> localeIds;

        for (const char* id = uenum_next(available.get(), nullptr, &status); id != nullptr;
             id = uenum_next(available.get(), nullptr, &status)) {
            localeIds.emplace(id);
        }

        requireSuccess(status, "ICU has no list of available locales");

        return localeIds;
    }();

    return availableLocaleIds.contains(localeId);
}

/**
 * A canonical tag up to its first singleton subtag, where the Unicode extension and private use sequences start. ICU
 * always writes a language subtag first, `und` when there is none, so a singleton never starts the tag.
 */
std::string withoutExtensions(std::string_view tag) {
    for (std::size_t start = 0; start < tag.size();) {
        const std::size_t end = std::min(tag.find('-', start), tag.size());

        if (end - start == 1) {
            return std::string(tag.substr(0, start - 1));
        }

        start = end + 1;
    }

    return std::string(tag);
}

struct ResolvedLocale {
    /** The locale ICU has data for, without extensions: `de-CH`. */
    std::string tag;
    /** The ICU locale ID of the requested locale it came from, with its keywords; empty for the default locale. */
    std::string requestedLocaleId;
};

/** ECMA-402's `LookupMatcher`: the first requested locale ICU has data for, dropping trailing subtags. */
ResolvedLocale resolveLocale(std::span<const std::string> requestedLocales, std::string_view defaultLocale) {
    for (const std::string& tag : canonicalizeLocaleList(requestedLocales)) {
        for (std::string candidate = withoutExtensions(tag); !candidate.empty();) {
            if (isAvailable(localeIdFromTag(candidate))) {
                return {candidate, localeIdFromTag(tag)};
            }

            const std::size_t separator = candidate.rfind('-');

            candidate.resize(separator == std::string::npos ? 0 : separator);
        }
    }

    return {std::string(defaultLocale), ""};
}

/** A Unicode extension keyword by its ICU name and value: `-u-kn` is `colnumeric=yes`, `-u-hc-h23` `hours=h23`. */
std::string keywordValue(const std::string& localeId, const char* keyword) {
    return readIcuString<char>(kInvalidLocale,
                               [&localeId, keyword](char* buffer, int32_t capacity, UErrorCode* status) {
                                   return uloc_getKeywordValue(localeId.c_str(), keyword, buffer, capacity, status);
                               });
}

/** Null for an identifier ICU knows but does not name a zone by, such as `Etc/Unknown` or a custom `GMT+5`. */
std::optional<std::string> canonicalTimeZone(std::string_view timeZone) {
    constexpr std::size_t kTimeZoneCapacity = 64;
    const std::u16string name = toUtf16(timeZone);
    std::array<UChar, kTimeZoneCapacity> buffer{};
    UBool isSystemId = 0;
    UErrorCode status = U_ZERO_ERROR;
    const int32_t length = ucal_getCanonicalTimeZoneID(name.data(), static_cast<int32_t>(name.size()), buffer.data(),
                                                       static_cast<int32_t>(buffer.size()), &isSystemId, &status);

    requireSuccess(status, "Invalid time zone specified: " + std::string(timeZone));

    if (isSystemId == 0) {
        return std::nullopt;
    }

    const std::string canonical = toUtf8({buffer.data(), static_cast<std::size_t>(length)});

    return canonical == "Etc/UTC" || canonical == "Etc/GMT" ? "UTC" : canonical;
}

std::string numberingSystem(const std::string& localeId) {
    UErrorCode status = U_ZERO_ERROR;
    const std::unique_ptr<UNumberingSystem, decltype(&unumsys_close)> system(unumsys_open(localeId.c_str(), &status),
                                                                             &unumsys_close);

    requireSuccess(status, "ICU has no numbering system for " + localeId);

    return unumsys_getName(system.get());
}

const IntlOptionValue* findOption(const IntlOptions& options, std::string_view name) {
    const auto option = std::ranges::find(options, name, &IntlOptions::value_type::first);

    return option == options.end() ? nullptr : &option->second;
}

void rejectUnsupportedOptions(const IntlOptions& options, std::span<const IntlOptionName> names,
                              std::string_view constructorName) {
    for (const auto& [name, value] : options) {
        if (!std::ranges::find(names, name, &IntlOptionName::name)->isSupported) {
            throw IntlRangeError(std::string(constructorName) + " does not support the option " + std::string(name) +
                                 " on this platform");
        }
    }
}

/** The choice the option names, null when it is absent; a value outside `choices` is a `RangeError`. */
const Choice* findChoice(const IntlOptions& options, std::string_view name, std::span<const Choice> choices) {
    const IntlOptionValue* value = findOption(options, name);

    if (value == nullptr) {
        return nullptr;
    }

    const std::string& text = std::get<std::string>(*value);
    const auto choice = std::ranges::find(choices, text, &Choice::value);

    if (choice == choices.end()) {
        throw IntlRangeError("Value " + text + " out of range for option " + std::string(name));
    }

    return &*choice;
}

std::optional<int> fractionDigitsOption(const IntlOptions& options, std::string_view name) {
    const IntlOptionValue* value = findOption(options, name);

    if (value == nullptr) {
        return std::nullopt;
    }

    const double digits = std::get<double>(*value);

    if (!(digits >= 0 && digits <= kMaximumFractionDigits)) {
        throw IntlRangeError(std::string(name) + " value is out of range.");
    }

    return static_cast<int>(digits);
}

constexpr std::array kLocaleMatcherChoices = {Choice{"lookup", ""}, Choice{"best fit", ""}};

void validateLocaleMatcher(const IntlOptions& options) { findChoice(options, "localeMatcher", kLocaleMatcherChoices); }

constexpr std::array kDateTimeFormatOptionNames = {
    IntlOptionName{"localeMatcher", IntlOptionType::String, true},
    IntlOptionName{"calendar", IntlOptionType::String, false},
    IntlOptionName{"numberingSystem", IntlOptionType::String, false},
    IntlOptionName{"hour12", IntlOptionType::Boolean, true},
    IntlOptionName{"hourCycle", IntlOptionType::String, true},
    IntlOptionName{"timeZone", IntlOptionType::String, true},
    IntlOptionName{"weekday", IntlOptionType::String, true},
    IntlOptionName{"era", IntlOptionType::String, false},
    IntlOptionName{"year", IntlOptionType::String, true},
    IntlOptionName{"month", IntlOptionType::String, true},
    IntlOptionName{"day", IntlOptionType::String, true},
    IntlOptionName{"dayPeriod", IntlOptionType::String, false},
    IntlOptionName{"hour", IntlOptionType::String, true},
    IntlOptionName{"minute", IntlOptionType::String, true},
    IntlOptionName{"second", IntlOptionType::String, true},
    IntlOptionName{"fractionalSecondDigits", IntlOptionType::Number, false},
    IntlOptionName{"timeZoneName", IntlOptionType::String, true},
    IntlOptionName{"formatMatcher", IntlOptionType::String, true},
    IntlOptionName{"dateStyle", IntlOptionType::String, true},
    IntlOptionName{"timeStyle", IntlOptionType::String, true},
};

constexpr std::array kWeekdayChoices = {Choice{"narrow", "EEEEE"}, Choice{"short", "EEE"}, Choice{"long", "EEEE"}};
constexpr std::array kYearChoices = {Choice{"2-digit", "yy"}, Choice{"numeric", "y"}};
constexpr std::array kMonthChoices = {Choice{"2-digit", "MM"}, Choice{"numeric", "M"}, Choice{"narrow", "MMMMM"},
                                      Choice{"short", "MMM"}, Choice{"long", "MMMM"}};
constexpr std::array kDayChoices = {Choice{"2-digit", "dd"}, Choice{"numeric", "d"}};
constexpr std::array kHourChoices = {Choice{"2-digit", "jj"}, Choice{"numeric", "j"}};
constexpr std::array kMinuteChoices = {Choice{"2-digit", "mm"}, Choice{"numeric", "m"}};
constexpr std::array kSecondChoices = {Choice{"2-digit", "ss"}, Choice{"numeric", "s"}};
constexpr std::array kTimeZoneNameChoices = {Choice{"short", "z"},        Choice{"long", "zzzz"},
                                             Choice{"shortOffset", "O"},  Choice{"longOffset", "OOOO"},
                                             Choice{"shortGeneric", "v"}, Choice{"longGeneric", "vvvv"}};

struct Component {
    std::string_view name;
    std::span<const Choice> choices;
};

constexpr std::array kComponents = {
    Component{"weekday", kWeekdayChoices}, Component{"year", kYearChoices},
    Component{"month", kMonthChoices},     Component{"day", kDayChoices},
    Component{"hour", kHourChoices},       Component{"minute", kMinuteChoices},
    Component{"second", kSecondChoices},   Component{"timeZoneName", kTimeZoneNameChoices},
};

constexpr std::array kStyleChoices = {Choice{"full", ""}, Choice{"long", ""}, Choice{"medium", ""},
                                      Choice{"short", ""}};
constexpr std::array kHourCycleChoices = {Choice{"h11", "K"}, Choice{"h12", "h"}, Choice{"h23", "H"},
                                          Choice{"h24", "k"}};
constexpr std::array kFormatMatcherChoices = {Choice{"basic", ""}, Choice{"best fit", ""}};
constexpr std::string_view kHourSymbols = "KhHk";

UDateFormatStyle styleOption(const IntlOptions& options, std::string_view name) {
    const Choice* style = findChoice(options, name, kStyleChoices);

    return style == nullptr ? UDAT_NONE : static_cast<UDateFormatStyle>(style - kStyleChoices.data());
}

/** The pattern symbol `hourCycle` and `hour12` ask for, `j` (the locale's own) when neither is passed. */
char hourSymbol(const IntlOptions& options) {
    const IntlOptionValue* hour12 = findOption(options, "hour12");

    if (hour12 != nullptr) {
        return std::get<bool>(*hour12) ? 'h' : 'H';
    }

    const Choice* hourCycle = findChoice(options, "hourCycle", kHourCycleChoices);

    return hourCycle == nullptr ? 'j' : hourCycle->symbol.front();
}

std::string resolveTimeZone(const IntlOptions& options, std::string_view defaultTimeZone) {
    const IntlOptionValue* timeZone = findOption(options, "timeZone");

    if (timeZone == nullptr) {
        return std::string(defaultTimeZone);
    }

    const std::string& name = std::get<std::string>(*timeZone);
    std::optional<std::string> canonical = canonicalTimeZone(name);

    if (!canonical.has_value()) {
        throw IntlRangeError("Invalid time zone specified: " + name);
    }

    return std::move(*canonical);
}

std::u16string bestPattern(const std::string& localeId, std::string_view skeleton) {
    UErrorCode status = U_ZERO_ERROR;
    const std::unique_ptr<UDateTimePatternGenerator, decltype(&udatpg_close)> generator(
        udatpg_open(localeId.c_str(), &status), &udatpg_close);

    requireSuccess(status, "ICU has no date patterns for " + localeId);

    const std::u16string skeletonText = toUtf16(skeleton);

    return readIcuString<char16_t>(
        "ICU could not match the skeleton " + std::string(skeleton),
        [&generator, &skeletonText](UChar* buffer, int32_t capacity, UErrorCode* patternStatus) {
            return udatpg_getBestPatternWithOptions(generator.get(), skeletonText.data(),
                                                    static_cast<int32_t>(skeletonText.size()),
                                                    UDATPG_MATCH_ALL_FIELDS_LENGTH, buffer, capacity, patternStatus);
        });
}

/** Where a pattern's hour fields are, outside its quoted literals: Basque writes `'ko'`, Scottish Gaelic `'mh'`. */
std::vector<std::size_t> hourSymbolPositions(std::u16string_view pattern) {
    std::vector<std::size_t> positions;
    bool isQuoted = false;

    for (std::size_t position = 0; position < pattern.size(); ++position) {
        if (pattern[position] == u'\'') {
            isQuoted = !isQuoted;
        } else if (!isQuoted && std::u16string_view(u"KhHk").find(pattern[position]) != std::u16string_view::npos) {
            positions.push_back(position);
        }
    }

    return positions;
}

/**
 * ICU's pattern generator answers `K` and `k` skeletons with the locale's `h` or `H`, so the hour cycle that was
 * asked for is written into the pattern afterwards, as V8 does.
 */
std::u16string withHourSymbol(std::u16string pattern, char hour) {
    if (hour == 'j') {
        return pattern;
    }

    for (const std::size_t position : hourSymbolPositions(pattern)) {
        pattern[position] = static_cast<char16_t>(hour);
    }

    return pattern;
}

std::string calendarName(const UDateFormat* format) {
    UErrorCode status = U_ZERO_ERROR;
    const std::string_view type = ucal_getType(udat_getCalendar(format), &status);

    requireSuccess(status, "ICU has no calendar type");

    return type == "gregorian" ? "gregory" : std::string(type);
}

/** ECMA-402's part type for each `UDateFormatField` the supported options and styles produce, as V8 maps them. */
constexpr std::array<std::pair<int32_t, std::string_view>, 22> kPartTypes = {{
    {UDAT_ERA_FIELD, "era"},
    {UDAT_YEAR_FIELD, "year"},
    {UDAT_MONTH_FIELD, "month"},
    {UDAT_STANDALONE_MONTH_FIELD, "month"},
    {UDAT_DATE_FIELD, "day"},
    {UDAT_DAY_OF_WEEK_FIELD, "weekday"},
    {UDAT_STANDALONE_DAY_FIELD, "weekday"},
    {UDAT_HOUR_OF_DAY1_FIELD, "hour"},
    {UDAT_HOUR_OF_DAY0_FIELD, "hour"},
    {UDAT_HOUR1_FIELD, "hour"},
    {UDAT_HOUR0_FIELD, "hour"},
    {UDAT_MINUTE_FIELD, "minute"},
    {UDAT_SECOND_FIELD, "second"},
    {UDAT_AM_PM_FIELD, "dayPeriod"},
    {UDAT_FLEXIBLE_DAY_PERIOD_FIELD, "dayPeriod"},
    {UDAT_TIMEZONE_FIELD, "timeZoneName"},
    {UDAT_TIMEZONE_RFC_FIELD, "timeZoneName"},
    {UDAT_TIMEZONE_GENERIC_FIELD, "timeZoneName"},
    {UDAT_TIMEZONE_SPECIAL_FIELD, "timeZoneName"},
    {UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD, "timeZoneName"},
    {UDAT_TIMEZONE_ISO_FIELD, "timeZoneName"},
    {UDAT_TIMEZONE_ISO_LOCAL_FIELD, "timeZoneName"},
}};

/** The text from `begin` to `end` as a part, unless it is empty; `field` -1 is the literal text between fields. */
void appendPart(IntlParts& parts, std::u16string_view text, int32_t field, std::size_t begin, std::size_t end) {
    if (begin == end) {
        return;
    }

    const auto partType = std::ranges::find(kPartTypes, field, &std::pair<int32_t, std::string_view>::first);

    parts.emplace_back(partType == kPartTypes.end() ? "literal" : partType->second,
                       toUtf8(text.substr(begin, end - begin)));
}

UDate timeValue(double epochMilliseconds) {
    if (!std::isfinite(epochMilliseconds) || std::abs(epochMilliseconds) > kMaximumTimeValue) {
        throw IntlRangeError("Invalid time value");
    }

    return std::trunc(epochMilliseconds);
}

std::u16string formatPattern(const UDateFormat* format) {
    return readIcuString<char16_t>("ICU has no pattern", [format](UChar* buffer, int32_t capacity, UErrorCode* status) {
        return udat_toPattern(format, false, buffer, capacity, status);
    });
}

constexpr std::array kNumberFormatOptionNames = {
    IntlOptionName{"localeMatcher", IntlOptionType::String, true},
    IntlOptionName{"numberingSystem", IntlOptionType::String, false},
    IntlOptionName{"style", IntlOptionType::String, true},
    IntlOptionName{"currency", IntlOptionType::String, true},
    IntlOptionName{"currencyDisplay", IntlOptionType::String, false},
    IntlOptionName{"currencySign", IntlOptionType::String, false},
    IntlOptionName{"unit", IntlOptionType::String, false},
    IntlOptionName{"unitDisplay", IntlOptionType::String, false},
    IntlOptionName{"notation", IntlOptionType::String, false},
    IntlOptionName{"minimumIntegerDigits", IntlOptionType::Number, false},
    IntlOptionName{"minimumFractionDigits", IntlOptionType::Number, true},
    IntlOptionName{"maximumFractionDigits", IntlOptionType::Number, true},
    IntlOptionName{"minimumSignificantDigits", IntlOptionType::Number, false},
    IntlOptionName{"maximumSignificantDigits", IntlOptionType::Number, false},
    IntlOptionName{"roundingIncrement", IntlOptionType::Number, false},
    IntlOptionName{"roundingMode", IntlOptionType::String, false},
    IntlOptionName{"roundingPriority", IntlOptionType::String, false},
    IntlOptionName{"trailingZeroDisplay", IntlOptionType::String, false},
    IntlOptionName{"compactDisplay", IntlOptionType::String, false},
    IntlOptionName{"useGrouping", IntlOptionType::String, false},
    IntlOptionName{"signDisplay", IntlOptionType::String, false},
};

constexpr std::array kNumberStyleChoices = {Choice{"decimal", ""}, Choice{"currency", ""}};
constexpr std::size_t kCurrencyCodeLength = 3;

std::optional<std::string> currencyOption(const IntlOptions& options) {
    const IntlOptionValue* value = findOption(options, "currency");

    if (value == nullptr) {
        return std::nullopt;
    }

    std::string code = std::get<std::string>(*value);
    const auto isAsciiLetter = [](char character) { return std::isalpha(static_cast<unsigned char>(character)) != 0; };

    if (code.size() != kCurrencyCodeLength || !std::ranges::all_of(code, isAsciiLetter)) {
        throw IntlRangeError("Invalid currency code : " + code);
    }

    std::ranges::transform(code, code.begin(), [](char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });

    return code;
}

int currencyFractionDigits(const std::string& currency) {
    const std::u16string code = toUtf16(currency);
    UErrorCode status = U_ZERO_ERROR;
    const int32_t digits = ucurr_getDefaultFractionDigits(code.c_str(), &status);

    requireSuccess(status, "ICU has no fraction digits for " + currency);

    return digits;
}

constexpr std::array kCollatorOptionNames = {
    IntlOptionName{"usage", IntlOptionType::String, true},
    IntlOptionName{"localeMatcher", IntlOptionType::String, true},
    IntlOptionName{"collation", IntlOptionType::String, false},
    IntlOptionName{"numeric", IntlOptionType::Boolean, true},
    IntlOptionName{"caseFirst", IntlOptionType::String, false},
    IntlOptionName{"sensitivity", IntlOptionType::String, true},
    IntlOptionName{"ignorePunctuation", IntlOptionType::Boolean, false},
};

constexpr std::array kUsageChoices = {Choice{"sort", ""}};
constexpr std::array kSensitivityChoices = {Choice{"base", ""}, Choice{"accent", ""}, Choice{"case", ""},
                                            Choice{"variant", ""}};
constexpr std::array kSensitivityStrengths = {UCOL_PRIMARY, UCOL_SECONDARY, UCOL_PRIMARY, UCOL_TERTIARY};
constexpr std::size_t kCaseSensitivity = 2;
constexpr std::size_t kVariantSensitivity = 3;

} // namespace

std::vector<std::string> canonicalizeLocaleList(std::span<const std::string> tags) {
    std::vector<std::string> canonical;

    for (const std::string& tag : tags) {
        std::string candidate = canonicalTag(tag);

        if (std::ranges::find(canonical, candidate) == canonical.end()) {
            canonical.push_back(std::move(candidate));
        }
    }

    return canonical;
}

std::string defaultIntlLocale(std::string_view posixLocale) {
    std::string tag(posixLocale);

    std::ranges::replace(tag, '_', '-');

    try {
        return resolveLocale(std::span(&tag, 1), kFallbackLocale).tag;
    } catch (const IntlRangeError&) {
        return std::string(kFallbackLocale);
    }
}

std::string defaultIntlTimeZone() {
    const std::u16string timeZone = readIcuString<char16_t>(
        "ICU has no default time zone", [](UChar* buffer, int32_t capacity, UErrorCode* status) {
            return ucal_getDefaultTimeZone(buffer, capacity, status);
        });

    return canonicalTimeZone(toUtf8(timeZone)).value_or("UTC");
}

std::span<const IntlOptionName> IntlDateTimeFormat::optionNames() noexcept { return kDateTimeFormatOptionNames; }

IntlDateTimeFormat::IntlDateTimeFormat(std::span<const std::string> requestedLocales, const IntlOptions& options,
                                       std::string_view defaultLocale, std::string_view defaultTimeZone)
    : format_(nullptr, &udat_close) {
    rejectUnsupportedOptions(options, kDateTimeFormatOptionNames, "Intl.DateTimeFormat");
    validateLocaleMatcher(options);
    findChoice(options, "formatMatcher", kFormatMatcherChoices);

    const ResolvedLocale resolved = resolveLocale(requestedLocales, defaultLocale);
    const std::string localeId = localeIdFromTag(resolved.tag);
    const std::string timeZone = resolveTimeZone(options, defaultTimeZone);
    const std::u16string timeZoneText = toUtf16(timeZone);
    const UDateFormatStyle dateStyle = styleOption(options, "dateStyle");
    const UDateFormatStyle timeStyle = styleOption(options, "timeStyle");
    const char requestedHour = hourSymbol(options);
    const std::string hourCycleExtension = keywordValue(resolved.requestedLocaleId, "hours");
    const auto extensionCycle = std::ranges::find(kHourCycleChoices, hourCycleExtension, &Choice::value);
    const bool honoursHourCycleExtension = extensionCycle != kHourCycleChoices.end() && timeStyle == UDAT_NONE &&
                                           findOption(options, "hour12") == nullptr &&
                                           (requestedHour == 'j' || requestedHour == extensionCycle->symbol.front());
    const char hour = honoursHourCycleExtension ? extensionCycle->symbol.front() : requestedHour;
    const bool hasStyle = dateStyle != UDAT_NONE || timeStyle != UDAT_NONE;
    const bool needsDefaults = !hasStyle && std::ranges::none_of(kComponents, [&options](const Component& component) {
        return component.name != "timeZoneName" && findOption(options, component.name) != nullptr;
    });

    IntlOptions effectiveOptions = options;

    if (needsDefaults) {
        for (const std::string_view name : {"year", "month", "day"}) {
            effectiveOptions.emplace_back(name, std::string("numeric"));
        }
    }

    std::string skeleton;
    IntlResolvedOptions components;

    for (const Component& component : kComponents) {
        const Choice* choice = findChoice(effectiveOptions, component.name, component.choices);

        if (choice == nullptr) {
            continue;
        }

        if (hasStyle) {
            throw IntlTypeError("Can't set option " + std::string(component.name) +
                                " when dateStyle or timeStyle is used");
        }

        skeleton += choice->symbol;
        components.emplace_back(component.name, std::string(choice->value));
    }

    UErrorCode status = U_ZERO_ERROR;

    if (hasStyle) {
        if (timeStyle != UDAT_NONE && hour != 'j') {
            throw IntlRangeError("Intl.DateTimeFormat does not support hourCycle or hour12 with timeStyle on this "
                                 "platform");
        }

        format_.reset(udat_open(timeStyle, dateStyle, localeId.c_str(), timeZoneText.data(),
                                static_cast<int32_t>(timeZoneText.size()), nullptr, 0, &status));
    } else {
        std::ranges::replace(skeleton, 'j', hour);

        const std::u16string pattern = withHourSymbol(bestPattern(localeId, skeleton), hour);

        format_.reset(udat_open(UDAT_PATTERN, UDAT_PATTERN, localeId.c_str(), timeZoneText.data(),
                                static_cast<int32_t>(timeZoneText.size()), pattern.data(),
                                static_cast<int32_t>(pattern.size()), &status));
    }

    requireSuccess(status, "ICU could not open a date format for " + localeId);

    resolvedOptions_ = {
        {"locale", honoursHourCycleExtension ? resolved.tag + "-u-hc-" + hourCycleExtension : resolved.tag},
        {"calendar", calendarName(format_.get())},
        {"numberingSystem", numberingSystem(localeId)},
        {"timeZone", timeZone}};

    const std::u16string pattern = formatPattern(format_.get());
    const std::vector<std::size_t> hourPositions = hourSymbolPositions(pattern);

    if (!hourPositions.empty()) {
        const std::size_t cycle = kHourSymbols.find(static_cast<char>(pattern[hourPositions.front()]));

        resolvedOptions_.emplace_back("hourCycle", std::string(kHourCycleChoices.at(cycle).value));
        resolvedOptions_.emplace_back("hour12", cycle < 2);
    }

    resolvedOptions_.insert(resolvedOptions_.end(), components.begin(), components.end());

    if (dateStyle != UDAT_NONE) {
        resolvedOptions_.emplace_back("dateStyle", std::string(kStyleChoices.at(dateStyle).value));
    }

    if (timeStyle != UDAT_NONE) {
        resolvedOptions_.emplace_back("timeStyle", std::string(kStyleChoices.at(timeStyle).value));
    }
}

std::string IntlDateTimeFormat::format(double epochMilliseconds) const {
    const UDate time = timeValue(epochMilliseconds);

    return toUtf8(readIcuString<char16_t>(
        "ICU could not format the date", [this, time](UChar* buffer, int32_t capacity, UErrorCode* status) {
            return udat_format(format_.get(), time, buffer, capacity, nullptr, status);
        }));
}

IntlParts IntlDateTimeFormat::formatToParts(double epochMilliseconds) const {
    const UDate time = timeValue(epochMilliseconds);
    UErrorCode status = U_ZERO_ERROR;
    const std::unique_ptr<UFieldPositionIterator, decltype(&ufieldpositer_close)> fields(ufieldpositer_open(&status),
                                                                                         &ufieldpositer_close);

    requireSuccess(status, "ICU could not open a field iterator");

    const std::u16string text =
        readIcuString<char16_t>("ICU could not format the date", [this, time, &fields](UChar* buffer, int32_t capacity,
                                                                                       UErrorCode* formatStatus) {
            return udat_formatForFields(format_.get(), time, buffer, capacity, fields.get(), formatStatus);
        });
    IntlParts parts;
    std::size_t position = 0;
    int32_t begin = 0;
    int32_t end = 0;

    for (int32_t field = ufieldpositer_next(fields.get(), &begin, &end); field >= 0;
         field = ufieldpositer_next(fields.get(), &begin, &end)) {
        appendPart(parts, text, -1, position, static_cast<std::size_t>(begin));
        appendPart(parts, text, field, static_cast<std::size_t>(begin), static_cast<std::size_t>(end));
        position = static_cast<std::size_t>(end);
    }

    appendPart(parts, text, -1, position, text.size());

    return parts;
}

const IntlResolvedOptions& IntlDateTimeFormat::resolvedOptions() const noexcept { return resolvedOptions_; }

std::span<const IntlOptionName> IntlNumberFormat::optionNames() noexcept { return kNumberFormatOptionNames; }

IntlNumberFormat::IntlNumberFormat(std::span<const std::string> requestedLocales, const IntlOptions& options,
                                   std::string_view defaultLocale)
    : formatter_(nullptr, &unumf_close), result_(nullptr, &unumf_closeResult) {
    rejectUnsupportedOptions(options, kNumberFormatOptionNames, "Intl.NumberFormat");
    validateLocaleMatcher(options);

    const std::string locale = resolveLocale(requestedLocales, defaultLocale).tag;
    const std::string localeId = localeIdFromTag(locale);
    const Choice* styleChoice = findChoice(options, "style", kNumberStyleChoices);
    const std::string_view style = styleChoice == nullptr ? "decimal" : styleChoice->value;
    const std::optional<std::string> currency = currencyOption(options);
    const bool isCurrency = style == "currency";

    if (isCurrency && !currency.has_value()) {
        throw IntlTypeError("Currency code is required with currency style.");
    }

    constexpr int kDecimalMaximumFractionDigits = 3;
    const int defaultMinimum = isCurrency ? currencyFractionDigits(*currency) : 0;
    const int defaultMaximum = isCurrency ? defaultMinimum : kDecimalMaximumFractionDigits;
    const std::optional<int> requestedMinimum = fractionDigitsOption(options, "minimumFractionDigits");
    const std::optional<int> requestedMaximum = fractionDigitsOption(options, "maximumFractionDigits");
    const int minimum = requestedMinimum.value_or(std::min(defaultMinimum, requestedMaximum.value_or(defaultMinimum)));
    const int maximum = requestedMaximum.value_or(std::max(defaultMaximum, minimum));

    if (minimum > maximum) {
        throw IntlRangeError("maximumFractionDigits value is out of range.");
    }

    std::string skeleton = "rounding-mode-half-up ";

    if (isCurrency) {
        skeleton += "currency/" + *currency + " ";
    }

    skeleton += maximum == 0 ? std::string("precision-integer")
                             : "." + std::string(static_cast<std::size_t>(minimum), '0') +
                                   std::string(static_cast<std::size_t>(maximum - minimum), '#');

    const std::u16string skeletonText = toUtf16(skeleton);
    UErrorCode status = U_ZERO_ERROR;

    formatter_.reset(unumf_openForSkeletonAndLocale(skeletonText.data(), static_cast<int32_t>(skeletonText.size()),
                                                    localeId.c_str(), &status));
    result_.reset(unumf_openResult(&status));
    requireSuccess(status, "ICU could not open a number format for " + localeId);

    resolvedOptions_ = {
        {"locale", locale}, {"numberingSystem", numberingSystem(localeId)}, {"style", std::string(style)}};

    if (isCurrency) {
        resolvedOptions_.insert(resolvedOptions_.end(), {{"currency", *currency},
                                                         {"currencyDisplay", std::string("symbol")},
                                                         {"currencySign", std::string("standard")}});
    }

    resolvedOptions_.insert(resolvedOptions_.end(), {{"minimumIntegerDigits", 1.0},
                                                     {"minimumFractionDigits", static_cast<double>(minimum)},
                                                     {"maximumFractionDigits", static_cast<double>(maximum)},
                                                     {"useGrouping", std::string("auto")},
                                                     {"notation", std::string("standard")},
                                                     {"signDisplay", std::string("auto")},
                                                     {"roundingIncrement", 1.0},
                                                     {"roundingMode", std::string("halfExpand")},
                                                     {"roundingPriority", std::string("auto")},
                                                     {"trailingZeroDisplay", std::string("auto")}});
}

std::string IntlNumberFormat::format(double value) const {
    UErrorCode status = U_ZERO_ERROR;

    unumf_formatDouble(formatter_.get(), value, result_.get(), &status);
    requireSuccess(status, "ICU could not format the number");

    return toUtf8(readIcuString<char16_t>("ICU could not read the formatted number",
                                          [this](UChar* buffer, int32_t capacity, UErrorCode* readStatus) {
                                              return unumf_resultToString(result_.get(), buffer, capacity, readStatus);
                                          }));
}

const IntlResolvedOptions& IntlNumberFormat::resolvedOptions() const noexcept { return resolvedOptions_; }

std::span<const IntlOptionName> IntlCollator::optionNames() noexcept { return kCollatorOptionNames; }

IntlCollator::IntlCollator(std::span<const std::string> requestedLocales, const IntlOptions& options,
                           std::string_view defaultLocale)
    : collator_(nullptr, &ucol_close) {
    rejectUnsupportedOptions(options, kCollatorOptionNames, "Intl.Collator");
    validateLocaleMatcher(options);
    findChoice(options, "usage", kUsageChoices);

    const ResolvedLocale resolved = resolveLocale(requestedLocales, defaultLocale);
    const std::string localeId = localeIdFromTag(resolved.tag);
    const Choice* sensitivityChoice = findChoice(options, "sensitivity", kSensitivityChoices);
    const std::size_t sensitivity = sensitivityChoice == nullptr
                                        ? kVariantSensitivity
                                        : static_cast<std::size_t>(sensitivityChoice - kSensitivityChoices.data());
    const IntlOptionValue* numericOption = findOption(options, "numeric");
    const std::string numericExtension = keywordValue(resolved.requestedLocaleId, "colnumeric");
    const bool numeric = numericOption == nullptr ? numericExtension == "yes" : std::get<bool>(*numericOption);
    const bool honoursNumericExtension = numericExtension == (numeric ? "yes" : "no");
    UErrorCode status = U_ZERO_ERROR;

    collator_.reset(ucol_open(localeId.c_str(), &status));
    ucol_setAttribute(collator_.get(), UCOL_STRENGTH, kSensitivityStrengths.at(sensitivity), &status);
    ucol_setAttribute(collator_.get(), UCOL_CASE_LEVEL, sensitivity == kCaseSensitivity ? UCOL_ON : UCOL_OFF, &status);
    ucol_setAttribute(collator_.get(), UCOL_NUMERIC_COLLATION, numeric ? UCOL_ON : UCOL_OFF, &status);
    requireSuccess(status, "ICU could not open a collator for " + localeId);

    resolvedOptions_ = {{"locale", resolved.tag + (honoursNumericExtension ? (numeric ? "-u-kn" : "-u-kn-false") : "")},
                        {"usage", std::string("sort")},
                        {"sensitivity", std::string(kSensitivityChoices.at(sensitivity).value)},
                        {"ignorePunctuation", false},
                        {"collation", std::string("default")},
                        {"numeric", numeric},
                        {"caseFirst", std::string("false")}};
}

int IntlCollator::compare(std::string_view left, std::string_view right) const {
    UErrorCode status = U_ZERO_ERROR;
    const UCollationResult result = ucol_strcollUTF8(collator_.get(), left.data(), static_cast<int32_t>(left.size()),
                                                     right.data(), static_cast<int32_t>(right.size()), &status);

    requireSuccess(status, "ICU could not compare the strings");

    return result;
}

const IntlResolvedOptions& IntlCollator::resolvedOptions() const noexcept { return resolvedOptions_; }

} // namespace react_native_linux
