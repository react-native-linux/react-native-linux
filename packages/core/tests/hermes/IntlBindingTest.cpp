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
              R"("hour12":false,"hour":"numeric","minute":"2-digit"})");
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
}

TEST_F(IntlBindingTest, CollatorSortsFileNamesNumericallyAndCaseInsensitively) {
    EXPECT_EQ(evaluate("['a10', 'a2', 'B1', 'b0'].sort(new Intl.Collator(undefined, {numeric: true, sensitivity: "
                       "'base'}).compare)"),
              R"(["a2","a10","b0","B1"])");
    EXPECT_EQ(evaluate("new Intl.Collator('en', {numeric: 1}).resolvedOptions()"),
              R"({"locale":"en","usage":"sort","sensitivity":"variant","ignorePunctuation":false,)"
              R"("collation":"default","numeric":true,"caseFirst":"false"})");
    EXPECT_EQ(evaluate("Intl.Collator().compare('a', 'b')"), "-1");
}

TEST_F(IntlBindingTest, GetCanonicalLocalesCanonicalizesAndRejectsWhatIsNotALocale) {
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(['und'])"), R"(["und"])");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales('EN-us')"), R"(["en-US"])");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales()"), "[]");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(['en_US'])"), "RangeError: Incorrect locale information provided");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales([5])"), "TypeError: Language ID should be string or object.");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales(null)"), "TypeError: Cannot convert null to object");
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
