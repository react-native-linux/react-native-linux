#include "IntlBinding.h"

#include "I18n.h"
#include "Intl.h"
#include "ReactHost.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <hermes/hermes.h>
#include <iterator>
#include <jsi/jsi.h>
#include <memory>
#include <regex>
#include <stdexcept>
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

    facebook::jsi::Runtime& runtime() { return *runtime_; }

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

TEST_F(IntlBindingTest, LocaleCanonicalizesAndAddsAndRemovesLikelySubtags) {
    EXPECT_EQ(evaluate("((locale) => [locale.toString(), locale.baseName, locale.language, locale.script, "
                       "locale.region])(new Intl.Locale('EN-latn-us-u-ca-gregory'))"),
              R"(["en-Latn-US-u-ca-gregory","en-Latn-US","en","Latn","US"])");
    EXPECT_EQ(evaluate("String(new Intl.Locale('en').maximize())"), R"("en-Latn-US")");
    EXPECT_EQ(evaluate("String(new Intl.Locale('en-Latn-US').minimize())"), R"("en")");
    EXPECT_EQ(evaluate("new Intl.Locale('en').maximize() instanceof Intl.Locale"), "true");
    EXPECT_EQ(evaluate("String(new Intl.Locale('und-x-private'))"), R"("und-x-private")");
    EXPECT_EQ(evaluate("String([new Intl.Locale('de').script, new Intl.Locale('de').region])"), R"(",")");
    EXPECT_EQ(evaluate("String(new Intl.Locale(new Intl.Locale('en-US'), {region: 'GB'}))"), R"("en-GB")");
    EXPECT_EQ(evaluate("Intl.getCanonicalLocales('und-x-private')"), R"(["und-x-private"])");
}

TEST_F(IntlBindingTest, LocaleNeedsNewAndATagAndRejectsWhatIsNotALocale) {
    EXPECT_EQ(evaluate("Intl.Locale('en')"), "TypeError: Constructor Intl.Locale requires 'new'");
    EXPECT_EQ(evaluate("new Intl.Locale()"),
              "TypeError: First argument to Intl.Locale constructor can't be empty or missing");
    EXPECT_EQ(evaluate("new Intl.Locale('en_US')"), "RangeError: Incorrect locale information provided");
    EXPECT_EQ(evaluate("new Intl.Locale('en', {numeric: true})"),
              "RangeError: Intl.Locale does not support the option numeric on this platform");
    EXPECT_EQ(evaluate("String(new Intl.Locale({toString() { return 'fr-CA'; }}))"), R"("fr-CA")");
    EXPECT_EQ(evaluate("Object.getOwnPropertyDescriptor(Intl.Locale.prototype, 'language').get.call({})"),
              "TypeError: Method Intl.Locale.prototype.language called on incompatible receiver");
    EXPECT_EQ(evaluate("Intl.Locale.prototype.maximize.call({})"),
              "TypeError: Method Intl.Locale.prototype.maximize called on incompatible receiver");
}

TEST_F(IntlBindingTest, DisplayNamesAnswerFormatjsIcuProbesAsAFixedEngineDoes) {
    EXPECT_EQ(evaluate("new Intl.DisplayNames(['en'], {type: 'region'}).of('CA')"), R"("Canada")");
    EXPECT_EQ(evaluate("new Intl.DisplayNames(['en'], {type: 'script'}).of('arab')"), R"("Arabic")");
    EXPECT_EQ(evaluate("new Intl.DisplayNames(['en'], {type: 'language'}).of('en-US')"), R"("American English")");
    EXPECT_EQ(evaluate("Intl.DisplayNames.supportedLocalesOf(['en'])"), R"(["en"])");
    EXPECT_EQ(evaluate("new Intl.DisplayNames('de', {type: 'region', style: 'short'}).resolvedOptions()"),
              R"({"locale":"de","style":"short","type":"region","fallback":"code"})");
    EXPECT_EQ(evaluate("String(new Intl.DisplayNames('en', {type: 'language', fallback: 'none'}).of('xyz'))"),
              R"("undefined")");
    EXPECT_EQ(evaluate("new Intl.DisplayNames('en', {type: 'region'}).of({toString() { return 'qm'; }})"), R"("QM")");
}

TEST_F(IntlBindingTest, DisplayNamesNeedNewAndATypeItImplements) {
    EXPECT_EQ(evaluate("Intl.DisplayNames('en', {type: 'region'})"),
              "TypeError: Constructor Intl.DisplayNames requires 'new'");
    EXPECT_EQ(evaluate("new Intl.DisplayNames('en')"), "TypeError: Required option 'type' is missing");
    EXPECT_EQ(evaluate("new Intl.DisplayNames('en', {type: 'currency'})"),
              "RangeError: Intl.DisplayNames does not support the type currency on this platform");
    EXPECT_EQ(evaluate("new Intl.DisplayNames('en', {type: 'region'}).of('1')"), "RangeError: Invalid region code: 1");
    EXPECT_EQ(evaluate("Intl.DisplayNames.prototype.of.call({}, 'CA')"),
              "TypeError: Method Intl.DisplayNames.prototype.of called on incompatible receiver");
}

TEST_F(IntlBindingTest, EveryServiceConstructorAnswersSupportedLocalesOf) {
    EXPECT_EQ(evaluate("[Intl.DateTimeFormat, Intl.NumberFormat, Intl.Collator].map((Service) => "
                       "Service.supportedLocalesOf(['de-DE', 'zz', 'EN-u-kn']))"),
              R"([["de-DE","en-u-kn"],["de-DE","en-u-kn"],["de-DE","en-u-kn"]])");
    EXPECT_EQ(evaluate("Intl.Collator.supportedLocalesOf('en', {localeMatcher: 'other'})"),
              "RangeError: Value other out of range for option localeMatcher");
    EXPECT_EQ(evaluate("Intl.NumberFormat.supportedLocalesOf('en_US')"),
              "RangeError: Incorrect locale information provided");
}

/**
 * Node's module resolution as far as the formatjs polyfills need it: a relative path, or a package found in the
 * nearest `node_modules` up the directory tree, entered by its `main` unless a subpath names the file; each is tried
 * as a file, with `.js`, and as a directory's `index.js`.
 */
std::string resolveModule(const std::filesystem::path& directory, const std::string& specifier) {
    std::filesystem::path base = directory / specifier;

    if (!specifier.starts_with('.')) {
        const std::size_t nameEnd = specifier.find('/', specifier.starts_with('@') ? specifier.find('/') + 1 : 0);
        const std::string name = specifier.substr(0, nameEnd);
        std::filesystem::path package = directory / "node_modules" / name;

        for (std::filesystem::path search = directory; !std::filesystem::exists(package);
             package = search / "node_modules" / name) {
            if (search == search.root_path()) {
                throw std::runtime_error("Cannot find the package " + name + " from " + directory.string());
            }

            search = search.parent_path();
        }

        std::ifstream manifestFile(package / "package.json");
        const std::string manifest{std::istreambuf_iterator<char>(manifestFile), std::istreambuf_iterator<char>()};
        std::smatch main;
        const bool hasSubpath = nameEnd != std::string::npos;

        base = hasSubpath ? package / specifier.substr(nameEnd + 1) : package;

        if (!hasSubpath && std::regex_search(manifest, main, std::regex(R"PATTERN("main"\s*:\s*"([^"]+)")PATTERN"))) {
            base /= main[1].str();
        }
    }

    for (const std::filesystem::path& candidate : {base, std::filesystem::path(base) += ".js", base / "index.js"}) {
        if (std::filesystem::is_regular_file(candidate)) {
            return std::filesystem::canonical(candidate).string();
        }
    }

    throw std::runtime_error("Cannot resolve " + specifier + " from " + directory.string());
}

constexpr char kCommonJsLoader[] = R"JAVASCRIPT(
const modules = {};
const load = (path) => {
  if (!(path in modules)) {
    const module = { exports: {} };
    const directory = path.slice(0, path.lastIndexOf("/"));

    modules[path] = module;
    new Function("require", "module", "exports", readModule(path))(
      (specifier) => load(resolveModule(directory, specifier)), module, module.exports);
  }
  return modules[path].exports;
};
globalThis.requireFromCore = (specifier) => load(resolveModule(RNL_CORE_PACKAGE_DIR, specifier));
)JAVASCRIPT";

// #772: T3 Code's intl-polyfills.ts imports these three in this order. Each installs itself only when the engine's
// Intl fails its probes; when the DisplayNames one does, its shouldPolyfill matches `en` against 571 locales through
// the Intl.Locale polyfill, which took about 22 minutes in a Debug build.
TEST_F(IntlBindingTest, TheFormatjsPolyfillsT3CodeImportsInstallNothingAndDecideQuickly) {
    constexpr std::chrono::seconds kPolyfillBudget{10};
    facebook::jsi::Runtime& runtime = this->runtime();

    runtime.global().setProperty(runtime, "RNL_CORE_PACKAGE_DIR", RNL_CORE_PACKAGE_DIR);
    runtime.global().setProperty(
        runtime, "resolveModule",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "resolveModule"), 2,
            [](facebook::jsi::Runtime& callRuntime, const facebook::jsi::Value& /*thisValue*/,
               const facebook::jsi::Value* arguments, size_t /*count*/) -> facebook::jsi::Value {
                return facebook::jsi::String::createFromUtf8(
                    callRuntime, resolveModule(arguments[0].getString(callRuntime).utf8(callRuntime),
                                               arguments[1].getString(callRuntime).utf8(callRuntime)));
            }));
    runtime.global().setProperty(
        runtime, "readModule",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "readModule"), 1,
            [](facebook::jsi::Runtime& callRuntime, const facebook::jsi::Value& /*thisValue*/,
               const facebook::jsi::Value* arguments, size_t /*count*/) -> facebook::jsi::Value {
                std::ifstream file(arguments[0].getString(callRuntime).utf8(callRuntime));

                return facebook::jsi::String::createFromUtf8(
                    callRuntime, std::string{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()});
            }));
    runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(kCommonJsLoader), "loader.js");

    const std::string engineIntl = "[Intl.getCanonicalLocales, Intl.Locale, Intl.DisplayNames]";

    evaluate("globalThis.engineIntl = " + engineIntl);

    const auto start = std::chrono::steady_clock::now();

    EXPECT_EQ(evaluate("['@formatjs/intl-getcanonicallocales/polyfill', '@formatjs/intl-locale/polyfill', "
                       "'@formatjs/intl-displaynames/polyfill', '@formatjs/intl-displaynames/locale-data/en']"
                       ".map(requireFromCore).length"),
              "4");
    EXPECT_LT(std::chrono::steady_clock::now() - start, kPolyfillBudget);
    EXPECT_EQ(evaluate(engineIntl + ".map((value, index) => value === engineIntl[index] && "
                                    "value.polyfilled === undefined)"),
              "[true,true,true]");
    EXPECT_EQ(evaluate("['intl-getcanonicallocales', 'intl-locale', 'intl-displaynames'].map((name) => "
                       "String(requireFromCore(`@formatjs/${name}/should-polyfill`).shouldPolyfill()))"),
              R"(["false","false","undefined"])");
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
