#include "IntlBinding.h"

#include "I18n.h"
#include "Intl.h"
#include "ReactHost.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <hermes/hermes.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

// #83: the Intl this platform installs, asserted from JavaScript, and a bundle that builds T3 Code's module-scope
// formatters starting through the host's real install path.

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

class IntlBindingTest : public ::testing::Test {
protected:
    IntlBindingTest() { installIntl(*runtime_); }

    std::string evaluate(const std::string& expression) {
        const std::string source = "(() => { try { return JSON.stringify(" + expression +
                                   "); } catch (error) { return error.name + ': ' + error.message; } })()";

        return runtime_->evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "intl-test.js")
            .getString(*runtime_)
            .utf8(*runtime_);
    }

private:
    std::unique_ptr<facebook::jsi::Runtime> runtime_{facebook::hermes::makeHermesRuntime()};
};

TEST_F(IntlBindingTest, DateTimeFormatFormatsWithTheLocalesPatternsAndResolvesItsOptions) {
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('de-DE', {dateStyle: 'long', timeZone: 'UTC'}).format(0)"),
              R"("1. Januar 1970")");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en-US', {dateStyle: 'long', timeZone: 'UTC'}).format(new Date(0))"),
              R"("January 1, 1970")");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en-US', {month: 'numeric', day: 'numeric', timeZone: 'UTC'})"
                       ".format(0)"),
              R"("1/1")");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en-US', {hour: 'numeric', minute: '2-digit', hourCycle: 'h23', "
                       "timeZone: 'UTC'}).resolvedOptions()"),
              R"({"locale":"en-US","calendar":"gregory","numberingSystem":"latn","timeZone":"UTC","hourCycle":"h23",)"
              R"("hour12":false,"hour":"2-digit","minute":"2-digit"})");
    EXPECT_EQ(evaluate("Intl.DateTimeFormat().resolvedOptions().locale"),
              "\"" + defaultIntlLocale(localeFromEnvironment()) + "\"");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat() instanceof Intl.DateTimeFormat"), "true");
    EXPECT_EQ(evaluate("typeof new Intl.DateTimeFormat('en-US').format()"), R"("string")");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en-US', {hour12: 0, hour: 'numeric', timeZone: 'UTC'}).format(0)"),
              R"("00")");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en-US').format(NaN)"), "RangeError: Invalid time value");
}

TEST_F(IntlBindingTest, NumberFormatGroupsForTheLocaleAndItsFormatIsBound) {
    EXPECT_EQ(evaluate("new Intl.NumberFormat('de-DE').format(1234567.891)"), R"("1.234.567,891")");
    EXPECT_EQ(evaluate("new Intl.NumberFormat('de-DE').resolvedOptions().locale"), R"("de-DE")");
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en-US').format(1234567.891)"), R"("1,234,567.891")");
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en-US', {style: 'currency', currency: 'USD', minimumFractionDigits: 2, "
                       "maximumFractionDigits: '2'}).format(1234.5)"),
              R"("$1,234.50")");
    EXPECT_EQ(evaluate("[1000, 2000].map(new Intl.NumberFormat('en-US').format)"), R"(["1,000","2,000"])");
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en-US').format()"), R"("NaN")");
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en-US').format('1234')"), R"("1,234")");
}

TEST_F(IntlBindingTest, EachInstanceBindsItsMethodOnceAsANonEnumerableOwnProperty) {
    EXPECT_EQ(evaluate("((format) => format.format === format.format)(new Intl.NumberFormat())"), "true");
    EXPECT_EQ(evaluate("((format) => format.format === format.format)(new Intl.DateTimeFormat())"), "true");
    EXPECT_EQ(evaluate("((collator) => [collator.compare === collator.compare, Object.keys(collator).length])"
                       "(new Intl.Collator())"),
              "[true,0]");
    EXPECT_EQ(evaluate("new Intl.Collator().compare !== new Intl.Collator().compare"), "true");
    EXPECT_EQ(
        evaluate("((collator) => ['b', 'c', 'a'].sort(collator.compare).concat(['z', 'y'].sort(collator.compare)))"
                 "(new Intl.Collator('en'))"),
        R"(["a","b","c","y","z"])");
    EXPECT_EQ(evaluate("Object.freeze(new Intl.Collator('en')).compare('a', 'b')"), "-1");
}

TEST_F(IntlBindingTest, CollatorSortsFileNamesNumericallyAndCaseInsensitively) {
    EXPECT_EQ(evaluate("['a10', 'a2', 'B1', 'b0'].sort(new Intl.Collator(undefined, {numeric: true, sensitivity: "
                       "'base'}).compare)"),
              R"(["a2","a10","b0","B1"])");
    EXPECT_EQ(evaluate("new Intl.Collator('en', {numeric: 1}).resolvedOptions()"),
              R"({"locale":"en","usage":"sort","sensitivity":"variant","ignorePunctuation":false,)"
              R"("collation":"default","numeric":true,"caseFirst":"false"})");
    EXPECT_EQ(evaluate("Intl.Collator().compare('a', 'b')"), "-1");
    EXPECT_EQ(evaluate("((order) => [new Intl.Collator('en').compare({toString() { order.push('left'); return 'b'; "
                       "}}, {toString() { order.push('right'); return 'a'; }}), order])([])"),
              R"([1,["left","right"]])");
}

TEST_F(IntlBindingTest, GetCanonicalLocalesCanonicalizesAndRejectsWhatIsNotALocale) {
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(['und'])"), R"(["und"])");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales('EN-us')"), R"(["en-US"])");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales()"), "[]");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(['en_US'])"), "RangeError: Incorrect locale information provided");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales([5])"), "TypeError: Language ID should be string or object.");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(null)"), "TypeError: Cannot convert null to object");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(['sh', 'sr-Latn'])"), R"(["sr-Latn"])");
}

TEST_F(IntlBindingTest, EachLocaleIsValidatedBeforeTheNextElementOrTheOptionsAreRead) {
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(['en_US', 5])"), "RangeError: Incorrect locale information provided");
    EXPECT_EQ(evaluate("new Intl.Collator(Object.defineProperty(['x-private'], 1, {get() { throw new Error('read'); "
                       "}}), {get numeric() { throw new Error('read options'); }})"),
              "RangeError: Incorrect locale information provided");
}

TEST_F(IntlBindingTest, GetCanonicalLocalesReadsALengthAsEcma262sToLength) {
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(Object.defineProperty({0: 'en', length: Infinity}, 1, "
                       "{get() { throw new Error('read past the first element'); }}))"),
              "Error: read past the first element");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales({0: 'en', length: NaN})"), "[]");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales({0: 'en', length: -Infinity})"), "[]");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales({0: 'en', length: -1})"), "[]");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales({0: 'en', 2: 'de', length: '3.9'})"), R"(["en","de"])");
}

TEST_F(IntlBindingTest, ToNumberRejectsABigIntWhereNumberFormatsOne) {
    const std::string bigIntToNumber = "TypeError: Cannot convert BigInt to number";

    EXPECT_EQ(evaluate("Intl.getCanonicalLocales({0: 'en', length: 1n})"), bigIntToNumber);
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en').format(1n)"), bigIntToNumber);
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en', {maximumFractionDigits: {valueOf() { return 1n; }}})"),
              bigIntToNumber);
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en').format(1n)"), R"("1")");
}

TEST_F(IntlBindingTest, ASubclassKeepsItsOwnPrototype) {
    EXPECT_EQ(evaluate("[Intl.DateTimeFormat, Intl.NumberFormat, Intl.Collator].map((Base) => { class Derived "
                       "extends Base { label() { return 'ok'; } } const instance = new Derived('en'); return "
                       "[instance.label(), instance instanceof Derived, instance.resolvedOptions().locale]; })"),
              R"([["ok",true,"en"],["ok",true,"en"],["ok",true,"en"]])");
    EXPECT_EQ(evaluate("((Target) => { Target.prototype = 1; return Object.getPrototypeOf(Reflect.construct("
                       "Intl.Collator, [], Target)) === Intl.Collator.prototype; })(function Target() {})"),
              "true");
}

TEST_F(IntlBindingTest, FormatToPartsServesT3CodesHourLabelsAndTimeZoneLabels) {
    const std::string hourLabel =
        "new Intl.DateTimeFormat('en-US', {timeZone: 'UTC', hour: '2-digit', hourCycle: 'h23'";
    const std::string formatHour =
        ".formatToParts(new Date(Date.UTC(2026, 6, 1, 13))).map((part) => (part.type === 'hour' ? `${part.value}:00` : "
        "part.value)).join('')";
    const std::string zoneLabel = "new Intl.DateTimeFormat('en-US', {timeZone: 'America/New_York', timeZoneName: ";
    const std::string findZone = ".find((part) => part.type === 'timeZoneName').value";

    EXPECT_EQ(evaluate(hourLabel + "})" + formatHour), R"("13:00")");
    EXPECT_EQ(evaluate(hourLabel + ", timeZoneName: 'short'})" + formatHour), R"("13:00 UTC")");
    EXPECT_EQ(evaluate(zoneLabel + "'short'}).formatToParts(Date.UTC(2026, 6, 1, 16))" + findZone), R"("EDT")");
    EXPECT_EQ(evaluate(zoneLabel + "'shortGeneric'}).formatToParts(undefined)" + findZone), R"("ET")");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat('en-US', {timeZone: 'UTC'}).formatToParts(0)"),
              R"([{"type":"month","value":"1"},{"type":"literal","value":"/"},{"type":"day","value":"1"},)"
              R"({"type":"literal","value":"/"},{"type":"year","value":"1970"}])");
    EXPECT_EQ(evaluate("new Intl.DateTimeFormat().formatToParts(NaN)"), "RangeError: Invalid time value");
    EXPECT_EQ(evaluate("Intl.DateTimeFormat.prototype.formatToParts.call({})"),
              "TypeError: Method Intl.DateTimeFormat.prototype.formatToParts called on incompatible receiver");
}

TEST_F(IntlBindingTest, UnsupportedOptionsAndForeignReceiversThrowTheErrorsEcma402Names) {
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en', {notation: 'compact'})"),
              "RangeError: Intl.NumberFormat does not support the option notation on this platform");
    EXPECT_EQ(evaluate("new Intl.NumberFormat('en', {style: 'currency'})"),
              "TypeError: Currency code is required with currency style.");
    EXPECT_EQ(evaluate("new Intl.Collator('en', null)"), "TypeError: Cannot convert null to object");
    EXPECT_EQ(evaluate("Intl.DateTimeFormat.prototype.resolvedOptions.call({})"),
              "TypeError: Method Intl.DateTimeFormat.prototype.resolvedOptions called on incompatible receiver");
    EXPECT_EQ(evaluate("Intl.Collator.prototype.compare"),
              "TypeError: Method Intl.Collator.prototype.compare called on incompatible receiver");
    EXPECT_EQ(evaluate("'hello'.toLocaleUpperCase()"), R"("HELLO")");
}

constexpr char kModuleScopeFormatters[] = R"JAVASCRIPT(
const TIME_FORMATTER = new Intl.DateTimeFormat(undefined, { hour: "numeric", minute: "2-digit" });
const DATE_FORMATTER = new Intl.DateTimeFormat(undefined, { month: "numeric", day: "numeric" });
const DATE_WITH_YEAR_FORMATTER = new Intl.DateTimeFormat(undefined, { month: "numeric", day: "numeric", year: "numeric" });
const linkedAtFormatter = new Intl.DateTimeFormat(undefined, { dateStyle: "medium" });
const fileNameCollator = new Intl.Collator(undefined, { numeric: true, sensitivity: "base" });
const DIFF_COUNT_FORMAT = new Intl.NumberFormat();
const CURRENCY = new Intl.NumberFormat("en-US", {
  style: "currency", currency: "USD", minimumFractionDigits: 2, maximumFractionDigits: 2,
});
const INTEGER = new Intl.NumberFormat("en-US");

[TIME_FORMATTER, DATE_FORMATTER, DATE_WITH_YEAR_FORMATTER, linkedAtFormatter].forEach((formatter) => formatter.format(0));
fileNameCollator.compare("a2", "a10");
DIFF_COUNT_FORMAT.format(1234);
CURRENCY.format(1.5);
INTEGER.format(1000);
__rnlMarkTestPassed();
)JAVASCRIPT";

TEST(IntlStartupTest, ABundleBuildingT3CodesModuleScopeFormattersStarts) {
    const std::filesystem::path bundlePath =
        std::filesystem::temp_directory_path() / "rnl-intl-module-scope-formatters-test.js";

    std::ofstream{bundlePath} << kModuleScopeFormatters;

    {
        ReactHost reactHost;

        reactHost.loadBundle(bundlePath.string());

        EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
        EXPECT_FALSE(reactHost.hasReportedFatalError());
        EXPECT_TRUE(reactHost.hasMarkedTestPassed());
    }

    std::filesystem::remove(bundlePath);
}

} // namespace
} // namespace react_native_linux
