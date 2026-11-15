#include "Intl.h"

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <string_view>
#include <unicode/ucal.h>
#include <variant>
#include <vector>

namespace {

using namespace std::string_literals;
using react_native_linux::canonicalizeLocaleList;
using react_native_linux::defaultIntlLocale;
using react_native_linux::defaultIntlTimeZone;
using react_native_linux::IntlCollator;
using react_native_linux::IntlDateTimeFormat;
using react_native_linux::IntlNumberFormat;
using react_native_linux::IntlOptions;
using react_native_linux::IntlRangeError;
using react_native_linux::IntlResolvedOptions;
using react_native_linux::IntlTypeError;

using Locales = std::vector<std::string>;

constexpr double kOneHour = 3'600'000;

std::string describe(const IntlResolvedOptions& options) {
    std::string description;

    for (const auto& [name, value] : options) {
        description += std::string(name) + "=";

        if (const auto* text = std::get_if<std::string>(&value)) {
            description += *text;
        } else if (const auto* flag = std::get_if<bool>(&value)) {
            description += *flag ? "true" : "false";
        } else {
            description += std::to_string(static_cast<int>(std::get<double>(value)));
        }

        description += " ";
    }

    return description;
}

IntlDateTimeFormat dateTimeFormat(const Locales& locales, const IntlOptions& options) {
    return {locales, options, "en-US", "UTC"};
}

template <typename Error, typename Body> std::string errorMessage(const Body& body) {
    try {
        body();
    } catch (const Error& error) {
        return error.what();
    }

    return "nothing was thrown";
}

std::string unsupportedOptionNames(std::span<const react_native_linux::IntlOptionName> names) {
    std::string unsupported;

    for (const auto& name : names) {
        unsupported += name.isSupported ? "" : std::string(name.name) + " ";
    }

    return unsupported + std::to_string(names.size());
}

TEST(IntlTest, EachConstructorListsEveryOptionEcma402DefinesAndWhichAreNotImplemented) {
    EXPECT_EQ(unsupportedOptionNames(IntlDateTimeFormat::optionNames()),
              "calendar numberingSystem era dayPeriod fractionalSecondDigits 20");
    EXPECT_EQ(unsupportedOptionNames(IntlNumberFormat::optionNames()),
              "numberingSystem currencyDisplay currencySign unit unitDisplay notation minimumIntegerDigits "
              "minimumSignificantDigits maximumSignificantDigits roundingIncrement roundingMode roundingPriority "
              "trailingZeroDisplay compactDisplay useGrouping signDisplay 21");
    EXPECT_EQ(unsupportedOptionNames(IntlCollator::optionNames()), "collation caseFirst ignorePunctuation 7");
}

TEST(IntlTest, LocaleListsAreCanonicalizedDeduplicatedAndValidated) {
    EXPECT_EQ(canonicalizeLocaleList(Locales{"EN-us", "und", "en-US", "zh-hant-tw"}),
              (Locales{"en-US", "und", "zh-Hant-TW"}));
    EXPECT_EQ(errorMessage<IntlRangeError>([] { canonicalizeLocaleList(Locales{""}); }),
              "Incorrect locale information provided");
    EXPECT_EQ(errorMessage<IntlRangeError>([] { canonicalizeLocaleList(Locales{"en_US"}); }),
              "Incorrect locale information provided");
}

TEST(IntlTest, TheDefaultLocaleComesFromThePosixLocaleOrIsEnglish) {
    EXPECT_EQ(defaultIntlLocale("de_DE"), "de-DE");
    EXPECT_EQ(defaultIntlLocale("C"), "en-US");
    EXPECT_EQ(defaultIntlLocale("POSIX"), "en-US");
    EXPECT_EQ(defaultIntlLocale(""), "en-US");
    EXPECT_EQ(defaultIntlLocale("xx_YY"), "en-US");
}

TEST(IntlTest, TheDefaultTimeZoneIsTheHostsOrUtc) {
    std::u16string original(64, u'\0');
    UErrorCode status = U_ZERO_ERROR;

    original.resize(static_cast<std::size_t>(
        ucal_getDefaultTimeZone(original.data(), static_cast<int32_t>(original.size()), &status)));

    ucal_setDefaultTimeZone(u"Europe/Berlin", &status);
    EXPECT_EQ(defaultIntlTimeZone(), "Europe/Berlin");
    ucal_setDefaultTimeZone(u"Etc/UTC", &status);
    EXPECT_EQ(defaultIntlTimeZone(), "UTC");
    ucal_setDefaultTimeZone(u"Etc/Unknown", &status);
    EXPECT_EQ(defaultIntlTimeZone(), "UTC");
    ucal_setDefaultTimeZone(original.c_str(), &status);
    ASSERT_TRUE(U_SUCCESS(status));
}

TEST(IntlTest, ARequestedLocaleFallsBackSubtagBySubtagAndThenToTheDefault) {
    EXPECT_EQ(describe(dateTimeFormat({"xx", "de-CH-1996"}, {}).resolvedOptions()).substr(0, 15), "locale=de-CH ca");
    EXPECT_EQ(describe(dateTimeFormat({"de-u-co-phonebk"}, {}).resolvedOptions()).substr(0, 12), "locale=de ca");
    EXPECT_EQ(describe(dateTimeFormat({"x-private"}, {}).resolvedOptions()).substr(0, 15), "locale=en-US ca");
    EXPECT_EQ(describe(IntlDateTimeFormat({}, {}, "fr-FR", "UTC").resolvedOptions()).substr(0, 15), "locale=fr-FR ca");
}

TEST(IntlTest, DateStylesFormatThroughTheLocalesOwnPatterns) {
    const IntlDateTimeFormat german = dateTimeFormat({"de-DE"}, {{"dateStyle", "long"s}, {"timeZone", "UTC"s}});

    EXPECT_EQ(german.format(0), "1. Januar 1970");
    EXPECT_EQ(describe(german.resolvedOptions()),
              "locale=de-DE calendar=gregory numberingSystem=latn timeZone=UTC dateStyle=long ");
    EXPECT_EQ(dateTimeFormat({"en-US"}, {{"dateStyle", "long"s}}).format(0), "January 1, 1970");
    EXPECT_EQ(describe(dateTimeFormat({"en-US"}, {{"timeStyle", "short"s}}).resolvedOptions()),
              "locale=en-US calendar=gregory numberingSystem=latn timeZone=UTC hourCycle=h12 hour12=true "
              "timeStyle=short ");
    EXPECT_EQ(dateTimeFormat({"en-US"}, {{"dateStyle", "short"s}, {"hourCycle", "h23"s}}).format(0), "1/1/70");
}

TEST(IntlTest, ComponentOptionsFormatThroughTheBestPatternForTheirSkeleton) {
    const IntlDateTimeFormat time = dateTimeFormat({"en-US"}, {{"hour", "numeric"s}, {"minute", "2-digit"s}});

    EXPECT_EQ(time.format(0), "12:00 AM");
    EXPECT_EQ(describe(time.resolvedOptions()), "locale=en-US calendar=gregory numberingSystem=latn timeZone=UTC "
                                                "hourCycle=h12 hour12=true hour=numeric minute=2-digit ");
    EXPECT_EQ(dateTimeFormat({"en-US"}, {{"month", "numeric"s}, {"day", "numeric"s}}).format(0), "1/1");
    EXPECT_EQ(dateTimeFormat({"en-US"}, {{"weekday", "long"s}, {"second", "2-digit"s}}).format(0), "Thursday, 00");
    EXPECT_EQ(dateTimeFormat({"en-US"}, {{"timeZoneName", "short"s}}).format(0), "1/1/1970, UTC");
}

TEST(IntlTest, NoComponentAndNoStyleMeansNumericYearMonthAndDay) {
    const IntlDateTimeFormat format = dateTimeFormat({"en-US"}, {});

    EXPECT_EQ(format.format(0), "1/1/1970");
    EXPECT_EQ(describe(format.resolvedOptions()),
              "locale=en-US calendar=gregory numberingSystem=latn timeZone=UTC year=numeric month=numeric "
              "day=numeric ");
}

TEST(IntlTest, HourCycleAndHour12ChooseTheHourSymbol) {
    const auto hourCycleOf = [](const IntlOptions& options) {
        IntlOptions withHour = options;

        withHour.emplace_back("hour", "2-digit"s);

        const std::string description = describe(dateTimeFormat({"en-US"}, withHour).resolvedOptions());
        const std::size_t start = description.find("hourCycle=");

        return description.substr(start, description.find(' ', start) - start);
    };

    EXPECT_EQ(hourCycleOf({{"hourCycle", "h11"s}}), "hourCycle=h11");
    EXPECT_EQ(hourCycleOf({{"hourCycle", "h12"s}}), "hourCycle=h12");
    EXPECT_EQ(hourCycleOf({{"hourCycle", "h23"s}}), "hourCycle=h23");
    EXPECT_EQ(hourCycleOf({{"hourCycle", "h24"s}}), "hourCycle=h24");
    EXPECT_EQ(hourCycleOf({{"hour12", false}, {"hourCycle", "h11"s}}), "hourCycle=h23");
    EXPECT_EQ(hourCycleOf({{"hour12", true}}), "hourCycle=h12");
    EXPECT_EQ(dateTimeFormat({"en-US"}, {{"hour", "2-digit"s}, {"hourCycle", "h23"s}}).format(13 * kOneHour), "13");
    EXPECT_EQ(
        dateTimeFormat({"fr-CA"}, {{"hour", "numeric"s}, {"minute", "2-digit"s}, {"hourCycle", "h24"s}}).format(0),
        "24 h 00");
}

TEST(IntlTest, TimeZonesAreValidatedAndCanonicalized) {
    const auto timeZoneOf = [](const std::string& timeZone) {
        const std::string description = describe(dateTimeFormat({"en-US"}, {{"timeZone", timeZone}}).resolvedOptions());
        const std::size_t start = description.find("timeZone=");

        return description.substr(start, description.find(' ', start) - start);
    };

    EXPECT_EQ(timeZoneOf("Europe/Berlin"), "timeZone=Europe/Berlin");
    EXPECT_EQ(timeZoneOf("UTC"), "timeZone=UTC");
    EXPECT_EQ(timeZoneOf("GMT"), "timeZone=UTC");
    EXPECT_EQ(errorMessage<IntlRangeError>([&] { timeZoneOf("Mars/Phobos"); }),
              "Invalid time zone specified: Mars/Phobos");
    EXPECT_EQ(errorMessage<IntlRangeError>([&] { timeZoneOf("Etc/Unknown"); }),
              "Invalid time zone specified: Etc/Unknown");
    EXPECT_EQ(
        dateTimeFormat({"en-US"}, {{"hour", "numeric"s}, {"hourCycle", "h23"s}, {"timeZone", "Asia/Tokyo"s}}).format(0),
        "09");
}

TEST(IntlTest, ANonGregorianLocaleReportsItsCalendarAndNumberingSystem) {
    EXPECT_EQ(describe(dateTimeFormat({"th"}, {}).resolvedOptions()).substr(0, 34),
              "locale=th calendar=buddhist number");
    EXPECT_EQ(describe(IntlNumberFormat(Locales{"ar-EG"}, {}, "en-US").resolvedOptions()).substr(0, 41),
              "locale=ar-EG numberingSystem=arab style=d");
}

TEST(IntlTest, DateTimeFormatRejectsWhatItDoesNotImplementOrWhatContradicts) {
    EXPECT_EQ(errorMessage<IntlRangeError>([] { dateTimeFormat({}, {{"era", "long"s}}); }),
              "Intl.DateTimeFormat does not support the option era on this platform");
    EXPECT_EQ(errorMessage<IntlRangeError>([] { dateTimeFormat({}, {{"month", "longest"s}}); }),
              "Value longest out of range for option month");
    EXPECT_EQ(errorMessage<IntlRangeError>([] { dateTimeFormat({}, {{"localeMatcher", "nearest"s}}); }),
              "Value nearest out of range for option localeMatcher");
    EXPECT_EQ(errorMessage<IntlTypeError>([] { dateTimeFormat({}, {{"dateStyle", "long"s}, {"month", "long"s}}); }),
              "Can't set option month when dateStyle or timeStyle is used");
    EXPECT_EQ(errorMessage<IntlRangeError>([] { dateTimeFormat({}, {{"timeStyle", "long"s}, {"hour12", true}}); }),
              "Intl.DateTimeFormat does not support hourCycle or hour12 with timeStyle on this platform");
    EXPECT_NO_THROW(dateTimeFormat({}, {{"localeMatcher", "lookup"s}, {"formatMatcher", "basic"s}}));
}

TEST(IntlTest, DatesOutsideTheTimeValueRangeAreRangeErrors) {
    const IntlDateTimeFormat format = dateTimeFormat({"en-US"}, {});

    EXPECT_EQ(errorMessage<IntlRangeError>([&] { format.format(std::nan("")); }), "Invalid time value");
    EXPECT_EQ(errorMessage<IntlRangeError>([&] { format.format(8.64e15 + 1); }), "Invalid time value");
    EXPECT_NO_THROW(format.format(-8.64e15));
    EXPECT_EQ(format.format(-0.5), "1/1/1970");
}

TEST(IntlTest, NumberFormatGroupsAndRoundsForTheLocale) {
    const IntlNumberFormat german(Locales{"de-DE"}, {}, "en-US");

    EXPECT_EQ(german.format(1234567.891), "1.234.567,891");
    EXPECT_EQ(describe(german.resolvedOptions()),
              "locale=de-DE numberingSystem=latn style=decimal minimumIntegerDigits=1 minimumFractionDigits=0 "
              "maximumFractionDigits=3 useGrouping=auto notation=standard signDisplay=auto roundingIncrement=1 "
              "roundingMode=halfExpand roundingPriority=auto trailingZeroDisplay=auto ");
    EXPECT_EQ(IntlNumberFormat(Locales{"en-US"}, {}, "de-DE").format(1234567.891), "1,234,567.891");
    EXPECT_EQ(IntlNumberFormat(Locales{}, {}, "en-US").format(std::nan("")), "NaN");
}

TEST(IntlTest, CurrencyFormatsWithTheCurrencysDigitsUnlessTold) {
    const IntlNumberFormat usageCost(
        Locales{"en-US"},
        {{"style", "currency"s}, {"currency", "USD"s}, {"minimumFractionDigits", 2.0}, {"maximumFractionDigits", 2.0}},
        "en-US");

    EXPECT_EQ(usageCost.format(1234.5), "$1,234.50");
    EXPECT_EQ(describe(usageCost.resolvedOptions()),
              "locale=en-US numberingSystem=latn style=currency currency=USD currencyDisplay=symbol "
              "currencySign=standard minimumIntegerDigits=1 minimumFractionDigits=2 maximumFractionDigits=2 "
              "useGrouping=auto notation=standard signDisplay=auto roundingIncrement=1 roundingMode=halfExpand "
              "roundingPriority=auto trailingZeroDisplay=auto ");
    EXPECT_EQ(IntlNumberFormat(Locales{"en-US"}, {{"style", "currency"s}, {"currency", "usd"s}}, "en-US").format(1),
              "$1.00");
    EXPECT_EQ(
        IntlNumberFormat(Locales{"en-US"}, {{"style", "currency"s}, {"currency", "JPY"s}}, "en-US").format(1234.5),
        "¥1,235");
    EXPECT_EQ(
        describe(IntlNumberFormat(Locales{"en-US"}, {{"currency", "EUR"s}}, "en-US").resolvedOptions()).substr(0, 61),
        "locale=en-US numberingSystem=latn style=decimal minimumIntege");
}

TEST(IntlTest, FractionDigitOptionsFollowEcma402sDefaults) {
    const auto digits = [](const IntlOptions& options) {
        const std::string description =
            describe(IntlNumberFormat(Locales{"en-US"}, options, "en-US").resolvedOptions());
        const std::size_t start = description.find("minimumFractionDigits=");

        return description.substr(start, description.find(" useGrouping") - start);
    };

    EXPECT_EQ(digits({{"minimumFractionDigits", 5.0}}), "minimumFractionDigits=5 maximumFractionDigits=5");
    EXPECT_EQ(digits({{"maximumFractionDigits", 1.9}}), "minimumFractionDigits=0 maximumFractionDigits=1");
    EXPECT_EQ(IntlNumberFormat(Locales{"en-US"}, {{"maximumFractionDigits", 1.0}}, "en-US").format(1.25), "1.3");
    EXPECT_EQ(IntlNumberFormat(Locales{"en-US"}, {{"maximumFractionDigits", 0.0}}, "en-US").format(2.5), "3");
    EXPECT_EQ(
        errorMessage<IntlRangeError>([&] { digits({{"minimumFractionDigits", 3.0}, {"maximumFractionDigits", 1.0}}); }),
        "maximumFractionDigits value is out of range.");
    EXPECT_EQ(errorMessage<IntlRangeError>([&] { digits({{"minimumFractionDigits", -1.0}}); }),
              "minimumFractionDigits value is out of range.");
    EXPECT_EQ(errorMessage<IntlRangeError>([&] { digits({{"maximumFractionDigits", 101.0}}); }),
              "maximumFractionDigits value is out of range.");
    EXPECT_EQ(errorMessage<IntlRangeError>([&] { digits({{"maximumFractionDigits", std::nan("")}}); }),
              "maximumFractionDigits value is out of range.");
}

TEST(IntlTest, NumberFormatRejectsWhatItDoesNotImplementOrWhatContradicts) {
    const auto failure = [](const IntlOptions& options) {
        return errorMessage<IntlRangeError>([&] { IntlNumberFormat(Locales{}, options, "en-US"); });
    };

    EXPECT_EQ(failure({{"notation", "compact"s}}),
              "Intl.NumberFormat does not support the option notation on this platform");
    EXPECT_EQ(failure({{"style", "percent"s}}), "Value percent out of range for option style");
    EXPECT_EQ(failure({{"currency", "US"s}}), "Invalid currency code : US");
    EXPECT_EQ(failure({{"currency", "U$D"s}}), "Invalid currency code : U$D");
    EXPECT_EQ(errorMessage<IntlTypeError>([] { IntlNumberFormat(Locales{}, {{"style", "currency"s}}, "en-US"); }),
              "Currency code is required with currency style.");
}

TEST(IntlTest, CollatorComparesWithTheRequestedSensitivityAndNumericOrdering) {
    const IntlCollator fileNames(Locales{}, {{"numeric", true}, {"sensitivity", "base"s}}, "en-US");
    std::vector<std::string> names{"a10", "a2", "B1", "b0"};

    std::ranges::sort(
        names, [&](const std::string& left, const std::string& right) { return fileNames.compare(left, right) < 0; });
    EXPECT_EQ(names, (std::vector<std::string>{"a2", "a10", "b0", "B1"}));
    EXPECT_EQ(fileNames.compare("a", "A"), 0);
    EXPECT_EQ(describe(fileNames.resolvedOptions()),
              "locale=en-US usage=sort sensitivity=base ignorePunctuation=false collation=default numeric=true "
              "caseFirst=false ");

    const auto compare = [](std::string_view sensitivity, std::string_view left, std::string_view right) {
        IntlOptions options;

        if (!sensitivity.empty()) {
            options.emplace_back("sensitivity", std::string(sensitivity));
        }

        return IntlCollator(Locales{"en"}, options, "en-US").compare(left, right);
    };

    EXPECT_EQ(compare("", "a", "A"), -1);
    EXPECT_EQ(compare("accent", "a", "A"), 0);
    EXPECT_EQ(compare("accent", "a", "á"), -1);
    EXPECT_EQ(compare("case", "a", "A"), -1);
    EXPECT_EQ(compare("case", "a", "á"), 0);
    EXPECT_EQ(compare("variant", "a10", "a2"), -1);
    EXPECT_EQ(IntlCollator(Locales{}, {{"numeric", false}, {"usage", "sort"s}}, "en-US").compare("a10", "a2"), -1);
}

TEST(IntlTest, CollatorRejectsWhatItDoesNotImplement) {
    EXPECT_EQ(errorMessage<IntlRangeError>([] { IntlCollator(Locales{}, {{"usage", "search"s}}, "en-US"); }),
              "Value search out of range for option usage");
    EXPECT_EQ(errorMessage<IntlRangeError>([] { IntlCollator(Locales{}, {{"ignorePunctuation", true}}, "en-US"); }),
              "Intl.Collator does not support the option ignorePunctuation on this platform");
}

} // namespace
