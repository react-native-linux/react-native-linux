#include "IntlBinding.h"

#include "I18n.h"
#include "Intl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace react_native_linux {

namespace {

namespace jsi = facebook::jsi;

template <typename Format> struct IntlState final : jsi::NativeState {
    template <typename... Arguments>
    explicit IntlState(Arguments&&... arguments) : format(std::forward<Arguments>(arguments)...) {}

    Format format;
};

template <typename Format>
using Construct = std::function<std::shared_ptr<IntlState<Format>>(std::span<const std::string>, const IntlOptions&)>;

template <typename Format>
using Method = std::function<jsi::Value(jsi::Runtime&, const Format&, const jsi::Value*, size_t)>;

const jsi::Value& argumentAt(const jsi::Value* arguments, size_t count, size_t index) {
    static const jsi::Value undefined;

    return index < count ? arguments[index] : undefined;
}

[[noreturn]] void throwJavaScriptError(jsi::Runtime& runtime, const char* constructorName, const std::string& message) {
    jsi::Value error = runtime.global()
                           .getPropertyAsFunction(runtime, constructorName)
                           .callAsConstructor(runtime, jsi::String::createFromUtf8(runtime, message));

    throw jsi::JSError(runtime, std::move(error));
}

template <typename Result, typename Body> Result translatingErrors(jsi::Runtime& runtime, const Body& body) {
    try {
        return body();
    } catch (const IntlRangeError& error) {
        throwJavaScriptError(runtime, "RangeError", error.what());
    } catch (const IntlTypeError& error) {
        throwJavaScriptError(runtime, "TypeError", error.what());
    }
}

jsi::Value callGlobal(jsi::Runtime& runtime, const char* functionName, const jsi::Value& argument) {
    return runtime.global().getPropertyAsFunction(runtime, functionName).call(runtime, argument);
}

/** ECMA-262's `ToNumber`, which throws on a BigInt where `Number()` converts it: `Math.max` of one value is it. */
double toNumber(jsi::Runtime& runtime, const jsi::Value& value) {
    if (value.isNumber()) {
        return value.getNumber();
    }

    return runtime.global()
        .getPropertyAsObject(runtime, "Math")
        .getPropertyAsFunction(runtime, "max")
        .call(runtime, value)
        .getNumber();
}

/** ECMA-402's `ToIntlMathematicalValue` as a double, in which a BigInt or a long decimal string loses digits (#682). */
double toIntlMathematicalValue(jsi::Runtime& runtime, const jsi::Value& value) {
    return value.isNumber() ? value.getNumber() : callGlobal(runtime, "Number", value).getNumber();
}

/** ECMA-262's `ToLength`: an integer from 0 to 2^53 - 1, NaN being 0. */
std::uint64_t toLength(jsi::Runtime& runtime, const jsi::Value& value) {
    constexpr double kMaximumLength = 9'007'199'254'740'991.0;
    const double length = toNumber(runtime, value);

    return std::isnan(length) ? 0 : static_cast<std::uint64_t>(std::clamp(std::trunc(length), 0.0, kMaximumLength));
}

/** The tag an `Intl.Locale` holds, which ECMA-402 reads from its slot rather than through `toString`. */
std::optional<std::string> localeTagOf(jsi::Runtime& runtime, const jsi::Value& value) {
    if (value.isObject()) {
        const jsi::Object object = value.getObject(runtime);

        if (object.hasNativeState<IntlState<IntlLocale>>(runtime)) {
            return object.getNativeState<IntlState<IntlLocale>>(runtime)->format.toString();
        }
    }

    return std::nullopt;
}

/**
 * ECMA-402's `CanonicalizeLocaleList` up to the canonicalization itself: a string, or a list of strings, each
 * validated before the next element is read.
 */
std::vector<std::string> readLocales(jsi::Runtime& runtime, const jsi::Value& locales) {
    if (locales.isUndefined()) {
        return {};
    }

    if (std::optional<std::string> tag = localeTagOf(runtime, locales)) {
        return {std::move(*tag)};
    }

    if (locales.isString()) {
        return {locales.getString(runtime).utf8(runtime)};
    }

    if (locales.isNull()) {
        throwJavaScriptError(runtime, "TypeError", "Cannot convert null to object");
    }

    if (!locales.isObject()) {
        return {};
    }

    const jsi::Object list = locales.getObject(runtime);
    const std::uint64_t length = toLength(runtime, list.getProperty(runtime, "length"));
    std::vector<std::string> tags;

    for (std::uint64_t index = 0; index < length; ++index) {
        const std::string key = std::to_string(index);

        if (!list.hasProperty(runtime, key.c_str())) {
            continue;
        }

        const jsi::Value element = list.getProperty(runtime, key.c_str());

        if (!element.isString() && !element.isObject()) {
            throwJavaScriptError(runtime, "TypeError", "Language ID should be string or object.");
        }

        std::optional<std::string> tag = localeTagOf(runtime, element);

        tags.push_back(tag.has_value() ? std::move(*tag) : element.toString(runtime).utf8(runtime));
        translatingErrors<std::vector<std::string>>(
            runtime, [&tags]() { return canonicalizeLocaleList(std::span(&tags.back(), 1)); });
    }

    return tags;
}

IntlOptionValue coerceOption(jsi::Runtime& runtime, const jsi::Value& value, IntlOptionType type) {
    switch (type) {
    case IntlOptionType::Boolean:
        return callGlobal(runtime, "Boolean", value).getBool();
    case IntlOptionType::Number:
        return toNumber(runtime, value);
    case IntlOptionType::String:
        break;
    }

    return value.toString(runtime).utf8(runtime);
}

IntlOptions readOptions(jsi::Runtime& runtime, const jsi::Value& options, std::span<const IntlOptionName> names) {
    if (options.isNull()) {
        throwJavaScriptError(runtime, "TypeError", "Cannot convert null to object");
    }

    if (!options.isObject()) {
        return {};
    }

    const jsi::Object object = options.getObject(runtime);
    IntlOptions values;

    for (const IntlOptionName& name : names) {
        const jsi::Value value =
            object.getProperty(runtime, jsi::PropNameID::forAscii(runtime, name.name.data(), name.name.size()));

        if (!value.isUndefined()) {
            values.emplace_back(name.name, coerceOption(runtime, value, name.type));
        }
    }

    return values;
}

jsi::Object toObject(jsi::Runtime& runtime, const IntlResolvedOptions& options) {
    jsi::Object object(runtime);

    for (const auto& [name, value] : options) {
        object.setProperty(runtime, jsi::PropNameID::forAscii(runtime, name.data(), name.size()),
                           std::visit(
                               [&runtime](const auto& alternative) -> jsi::Value {
                                   if constexpr (std::is_same_v<std::decay_t<decltype(alternative)>, std::string>) {
                                       return jsi::String::createFromUtf8(runtime, alternative);
                                   } else {
                                       return {alternative};
                                   }
                               },
                               value));
    }

    return object;
}

template <typename Format>
std::shared_ptr<IntlState<Format>> stateOf(jsi::Runtime& runtime, const jsi::Value& thisValue,
                                           const std::string& methodName) {
    if (thisValue.isObject()) {
        const jsi::Object object = thisValue.getObject(runtime);

        if (object.hasNativeState<IntlState<Format>>(runtime)) {
            return object.getNativeState<IntlState<Format>>(runtime);
        }
    }

    throwJavaScriptError(runtime, "TypeError", "Method " + methodName + " called on incompatible receiver");
}

jsi::Function hostFunction(jsi::Runtime& runtime, const char* name, unsigned int length,
                           jsi::HostFunctionType function) {
    return jsi::Function::createFromHostFunction(runtime, jsi::PropNameID::forAscii(runtime, name), length,
                                                 std::move(function));
}

jsi::Array toArray(jsi::Runtime& runtime, const std::vector<std::string>& strings) {
    jsi::Array array(runtime, strings.size());

    for (size_t index = 0; index < strings.size(); ++index) {
        array.setValueAtIndex(runtime, index, jsi::String::createFromUtf8(runtime, strings[index]));
    }

    return array;
}

void defineGetter(jsi::Runtime& runtime, const jsi::Object& object, const char* name, const jsi::Function& getter) {
    jsi::Object descriptor(runtime);

    descriptor.setProperty(runtime, "get", getter);
    descriptor.setProperty(runtime, "configurable", true);
    runtime.global()
        .getPropertyAsObject(runtime, "Object")
        .getPropertyAsFunction(runtime, "defineProperty")
        .call(runtime, object, name, descriptor);
}

/**
 * One `Intl` constructor over `create`, a host function that takes the first argument, the options and the prototype
 * of the instance to make. Returns the prototype.
 */
jsi::Object installConstructor(jsi::Runtime& runtime, jsi::Object& intl, const std::string& name, bool isCallable,
                               const jsi::Function& create) {
    jsi::Object prototype(runtime);
    const std::string requireNew =
        isCallable
            ? ""
            : "if (new.target === undefined) throw new TypeError(\"Constructor Intl." + name + " requires 'new'\"); ";

    // Hermes refuses `new` on a JSI host function ("This function cannot be used as a constructor"), so the
    // constructor is a plain function that returns what the host function made, which `new` then yields. It takes
    // the prototype from `new.target` as `OrdinaryCreateFromConstructor` does, so a subclass keeps its own, and the
    // JavaScript closure holds the intrinsic one, so no host closure holds a JavaScript object.
    const jsi::Function constructor =
        runtime.global()
            .getPropertyAsFunction(runtime, "Function")
            .callAsConstructor(runtime, "create", "intrinsicPrototype",
                               "return function " + name + "(first, options) { " + requireNew +
                                   "const prototype = (new.target || " + name +
                                   ").prototype; return create(first, options, Object(prototype) === prototype ? "
                                   "prototype : intrinsicPrototype); };")
            .asObject(runtime)
            .asFunction(runtime)
            .call(runtime, create, prototype)
            .asObject(runtime)
            .asFunction(runtime);

    prototype.setProperty(runtime, "constructor", constructor);
    constructor.setProperty(runtime, "prototype", prototype);
    intl.setProperty(runtime, name.c_str(), constructor);

    return prototype;
}

jsi::Value instanceWith(jsi::Runtime& runtime, const jsi::Value& prototype, std::shared_ptr<jsi::NativeState> state) {
    jsi::Object instance = jsi::Object::create(runtime, prototype);

    instance.setNativeState(runtime, std::move(state));

    return instance;
}

/** A prototype method, which reads its instance's state and throws the `Intl` errors as JavaScript ones. */
template <typename Format>
jsi::Function method(jsi::Runtime& runtime, const std::string& constructorName, const char* name, unsigned int length,
                     Method<Format> body) {
    return hostFunction(runtime, name, length,
                        [body = std::move(body), qualifiedName = "Intl." + constructorName + ".prototype." + name](
                            jsi::Runtime& callRuntime, const jsi::Value& thisValue, const jsi::Value* arguments,
                            size_t count) -> jsi::Value {
                            const std::shared_ptr<IntlState<Format>> state =
                                stateOf<Format>(callRuntime, thisValue, qualifiedName);

                            return translatingErrors<jsi::Value>(
                                callRuntime, [&]() { return body(callRuntime, state->format, arguments, count); });
                        });
}

/** A service's `locales` and `options` arguments, read in ECMA-402's order. */
std::pair<std::vector<std::string>, IntlOptions> readServiceArguments(jsi::Runtime& runtime,
                                                                      const jsi::Value* arguments, size_t count,
                                                                      std::span<const IntlOptionName> names) {
    std::vector<std::string> locales = readLocales(runtime, argumentAt(arguments, count, 0));

    return {std::move(locales), readOptions(runtime, argumentAt(arguments, count, 1), names)};
}

/**
 * An `Intl` service constructor (`DateTimeFormat`, `NumberFormat`, `Collator`, `DisplayNames`): a locale list and
 * options, `resolvedOptions` and `supportedLocalesOf`. Returns the prototype, for each one's own methods.
 */
template <typename Format>
jsi::Object installService(jsi::Runtime& runtime, jsi::Object& intl, const std::string& name, bool isCallable,
                           Construct<Format> construct) {
    jsi::Object prototype = installConstructor(
        runtime, intl, name, isCallable,
        hostFunction(runtime, name.c_str(), 0,
                     [construct = std::move(construct)](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                                        const jsi::Value* arguments, size_t count) -> jsi::Value {
                         const auto [locales, options] =
                             readServiceArguments(callRuntime, arguments, count, Format::optionNames());

                         return instanceWith(callRuntime, argumentAt(arguments, count, 2),
                                             translatingErrors<std::shared_ptr<IntlState<Format>>>(
                                                 callRuntime, [&]() { return construct(locales, options); }));
                     }));

    prototype.setProperty(
        runtime, "resolvedOptions",
        method<Format>(runtime, name, "resolvedOptions", 0,
                       [](jsi::Runtime& callRuntime, const Format& format, const jsi::Value* /*arguments*/,
                          size_t /*count*/) -> jsi::Value { return toObject(callRuntime, format.resolvedOptions()); }));
    intl.getPropertyAsObject(runtime, name.c_str())
        .setProperty(runtime, "supportedLocalesOf",
                     hostFunction(runtime, "supportedLocalesOf", 1,
                                  [](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                     const jsi::Value* arguments, size_t count) -> jsi::Value {
                                      const auto [locales, options] = readServiceArguments(
                                          callRuntime, arguments, count, kSupportedLocalesOptionNames);

                                      return toArray(callRuntime,
                                                     translatingErrors<std::vector<std::string>>(callRuntime, [&]() {
                                                         return supportedLocales(locales, options);
                                                     }));
                                  }));

    return prototype;
}

/**
 * The method that `format` and `compare` are: a getter returning a function bound to the instance, which ECMA-402
 * keeps in an internal slot so every read returns the same one.
 */
template <typename Format>
void installBoundMethod(jsi::Runtime& runtime, const jsi::Object& prototype, const std::string& constructorName,
                        const char* name, unsigned int length, Method<Format> body) {
    const jsi::Function getter = hostFunction(
        runtime, name, 0,
        [body = std::move(body), qualifiedName = "Intl." + constructorName + ".prototype." + name,
         name = std::string(name), length](jsi::Runtime& getterRuntime, const jsi::Value& thisValue,
                                           const jsi::Value* /*arguments*/, size_t /*count*/) -> jsi::Value {
            const std::shared_ptr<IntlState<Format>> state = stateOf<Format>(getterRuntime, thisValue, qualifiedName);
            jsi::Function bound = hostFunction(getterRuntime, "", length,
                                               [body, state](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                                             const jsi::Value* arguments, size_t count) -> jsi::Value {
                                                   return translatingErrors<jsi::Value>(callRuntime, [&]() {
                                                       return body(callRuntime, state->format, arguments, count);
                                                   });
                                               });
            jsi::Object descriptor(getterRuntime);

            // A non-enumerable own property is that slot here: the native state cannot hold it without a cycle the
            // collector does not see. Reflect leaves a frozen instance uncached rather than throwing.
            descriptor.setProperty(getterRuntime, "value", bound);
            descriptor.setProperty(getterRuntime, "configurable", true);
            getterRuntime.global()
                .getPropertyAsObject(getterRuntime, "Reflect")
                .getPropertyAsFunction(getterRuntime, "defineProperty")
                .call(getterRuntime, thisValue, name, descriptor);

            return bound;
        });

    defineGetter(runtime, prototype, name, getter);
}

/** ECMA-402's `Intl.Locale` tag argument: a string, or an object, which an `Intl.Locale` passes as its tag. */
std::string readLocaleTag(jsi::Runtime& runtime, const jsi::Value& tag) {
    if (std::optional<std::string> locale = localeTagOf(runtime, tag)) {
        return std::move(*locale);
    }

    if (!tag.isString() && !tag.isObject()) {
        throwJavaScriptError(runtime, "TypeError",
                             "First argument to Intl.Locale constructor can't be empty or missing");
    }

    return tag.toString(runtime).utf8(runtime);
}

jsi::Value optionalString(jsi::Runtime& runtime, const std::optional<std::string>& text) {
    return text.has_value() ? jsi::Value(jsi::String::createFromUtf8(runtime, *text)) : jsi::Value::undefined();
}

void installLocale(jsi::Runtime& runtime, jsi::Object& intl) {
    const jsi::Object prototype = installConstructor(
        runtime, intl, "Locale", false,
        hostFunction(runtime, "Locale", 0,
                     [](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/, const jsi::Value* arguments,
                        size_t count) -> jsi::Value {
                         const std::string tag = readLocaleTag(callRuntime, argumentAt(arguments, count, 0));
                         const IntlOptions options =
                             readOptions(callRuntime, argumentAt(arguments, count, 1), IntlLocale::optionNames());

                         return instanceWith(
                             callRuntime, argumentAt(arguments, count, 2),
                             translatingErrors<std::shared_ptr<IntlState<IntlLocale>>>(
                                 callRuntime, [&]() { return std::make_shared<IntlState<IntlLocale>>(tag, options); }));
                     }));
    const auto text = [](std::string (IntlLocale::*read)() const) -> Method<IntlLocale> {
        return
            [read](jsi::Runtime& runtime, const IntlLocale& locale, const jsi::Value* /*arguments*/,
                   size_t /*count*/) -> jsi::Value { return jsi::String::createFromUtf8(runtime, (locale.*read)()); };
    };
    const auto optionalText = [](std::optional<std::string> (IntlLocale::*read)() const) -> Method<IntlLocale> {
        return [read](jsi::Runtime& runtime, const IntlLocale& locale, const jsi::Value* /*arguments*/,
                      size_t /*count*/) -> jsi::Value { return optionalString(runtime, (locale.*read)()); };
    };

    prototype.setProperty(runtime, "toString",
                          method<IntlLocale>(runtime, "Locale", "toString", 0,
                                             [](jsi::Runtime& callRuntime, const IntlLocale& locale,
                                                const jsi::Value* /*arguments*/, size_t /*count*/) -> jsi::Value {
                                                 return jsi::String::createFromUtf8(callRuntime, locale.toString());
                                             }));
    defineGetter(runtime, prototype, "baseName",
                 method<IntlLocale>(runtime, "Locale", "baseName", 0, text(&IntlLocale::baseName)));
    defineGetter(runtime, prototype, "language",
                 method<IntlLocale>(runtime, "Locale", "language", 0, text(&IntlLocale::language)));
    defineGetter(runtime, prototype, "script",
                 method<IntlLocale>(runtime, "Locale", "script", 0, optionalText(&IntlLocale::script)));
    defineGetter(runtime, prototype, "region",
                 method<IntlLocale>(runtime, "Locale", "region", 0, optionalText(&IntlLocale::region)));

    // ECMA-402 makes maximize and minimize `Construct(%Intl.Locale%, tag)`, which only the constructor's own
    // JavaScript closure can do without a host closure holding a JavaScript object.
    runtime.global()
        .getPropertyAsFunction(runtime, "Function")
        .callAsConstructor(
            runtime, "Locale", "maximal", "minimal",
            "Locale.prototype.maximize = function maximize() { return new Locale(maximal.call(this)); }; "
            "Locale.prototype.minimize = function minimize() { return new Locale(minimal.call(this)); };")
        .asObject(runtime)
        .asFunction(runtime)
        .call(runtime, intl.getProperty(runtime, "Locale"),
              method<IntlLocale>(runtime, "Locale", "maximize", 0, text(&IntlLocale::maximize)),
              method<IntlLocale>(runtime, "Locale", "minimize", 0, text(&IntlLocale::minimize)));
}

struct IntlDefaults {
    std::string locale;
    std::string timeZone;
};

/**
 * Resolved on the first `Intl` object rather than at install, because ICU's locale list and time zone detection
 * cost milliseconds that a bundle which never formats anything should not pay at startup. The environment and the
 * host's zone are read once per process, as ICU itself caches its default zone.
 */
const IntlDefaults& defaults() {
    static const IntlDefaults kDefaults{defaultIntlLocale(localeFromEnvironment()), defaultIntlTimeZone()};

    return kDefaults;
}

/** The time a `DateTimeFormat` method formats: now when `date` is `undefined`, else `ToNumber(date)`. */
double timeOf(jsi::Runtime& runtime, const jsi::Value& date) {
    if (date.isUndefined()) {
        return std::trunc(
            std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count());
    }

    return toNumber(runtime, date);
}

jsi::Array partsToArray(jsi::Runtime& runtime, const IntlParts& parts) {
    jsi::Array array(runtime, parts.size());

    for (size_t index = 0; index < parts.size(); ++index) {
        jsi::Object part(runtime);

        part.setProperty(runtime, "type",
                         jsi::String::createFromAscii(runtime, parts[index].first.data(), parts[index].first.size()));
        part.setProperty(runtime, "value", jsi::String::createFromUtf8(runtime, parts[index].second));
        array.setValueAtIndex(runtime, index, part);
    }

    return array;
}

} // namespace

void installIntl(jsi::Runtime& runtime) {
    jsi::Object intl(runtime);

    const jsi::Object dateTimeFormatPrototype = installService<IntlDateTimeFormat>(
        runtime, intl, "DateTimeFormat", true, [](std::span<const std::string> locales, const IntlOptions& options) {
            return std::make_shared<IntlState<IntlDateTimeFormat>>(locales, options, defaults().locale,
                                                                   defaults().timeZone);
        });

    installBoundMethod<IntlDateTimeFormat>(runtime, dateTimeFormatPrototype, "DateTimeFormat", "format", 1,
                                           [](jsi::Runtime& callRuntime, const IntlDateTimeFormat& format,
                                              const jsi::Value* arguments, size_t count) -> jsi::Value {
                                               return jsi::String::createFromUtf8(
                                                   callRuntime,
                                                   format.format(timeOf(callRuntime, argumentAt(arguments, count, 0))));
                                           });
    dateTimeFormatPrototype.setProperty(
        runtime, "formatToParts",
        method<IntlDateTimeFormat>(runtime, "DateTimeFormat", "formatToParts", 1,
                                   [](jsi::Runtime& callRuntime, const IntlDateTimeFormat& format,
                                      const jsi::Value* arguments, size_t count) -> jsi::Value {
                                       return partsToArray(
                                           callRuntime,
                                           format.formatToParts(timeOf(callRuntime, argumentAt(arguments, count, 0))));
                                   }));

    installBoundMethod<IntlNumberFormat>(
        runtime,
        installService<IntlNumberFormat>(runtime, intl, "NumberFormat", true,
                                         [](std::span<const std::string> locales, const IntlOptions& options) {
                                             return std::make_shared<IntlState<IntlNumberFormat>>(locales, options,
                                                                                                  defaults().locale);
                                         }),
        "NumberFormat", "format", 1,
        [](jsi::Runtime& callRuntime, const IntlNumberFormat& format, const jsi::Value* arguments,
           size_t count) -> jsi::Value {
            return jsi::String::createFromUtf8(
                callRuntime, format.format(toIntlMathematicalValue(callRuntime, argumentAt(arguments, count, 0))));
        });

    installBoundMethod<IntlCollator>(
        runtime,
        installService<IntlCollator>(runtime, intl, "Collator", true,
                                     [](std::span<const std::string> locales, const IntlOptions& options) {
                                         return std::make_shared<IntlState<IntlCollator>>(locales, options,
                                                                                          defaults().locale);
                                     }),
        "Collator", "compare", 2,
        [](jsi::Runtime& callRuntime, const IntlCollator& collator, const jsi::Value* arguments,
           size_t count) -> jsi::Value {
            const std::string left = argumentAt(arguments, count, 0).toString(callRuntime).utf8(callRuntime);
            const std::string right = argumentAt(arguments, count, 1).toString(callRuntime).utf8(callRuntime);

            return collator.compare(left, right);
        });

    installService<IntlDisplayNames>(runtime, intl, "DisplayNames", false,
                                     [](std::span<const std::string> locales, const IntlOptions& options) {
                                         return std::make_shared<IntlState<IntlDisplayNames>>(locales, options,
                                                                                              defaults().locale);
                                     })
        .setProperty(
            runtime, "of",
            method<IntlDisplayNames>(runtime, "DisplayNames", "of", 1,
                                     [](jsi::Runtime& callRuntime, const IntlDisplayNames& names,
                                        const jsi::Value* arguments, size_t count) -> jsi::Value {
                                         const std::string code =
                                             argumentAt(arguments, count, 0).toString(callRuntime).utf8(callRuntime);

                                         return optionalString(callRuntime, names.of(code));
                                     }));

    installLocale(runtime, intl);

    intl.setProperty(runtime, "getCanonicalLocales",
                     hostFunction(runtime, "getCanonicalLocales", 1,
                                  [](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                     const jsi::Value* arguments, size_t count) -> jsi::Value {
                                      return toArray(callRuntime,
                                                     translatingErrors<std::vector<std::string>>(callRuntime, [&]() {
                                                         return canonicalizeLocaleList(
                                                             readLocales(callRuntime, argumentAt(arguments, count, 0)));
                                                     }));
                                  }));

    runtime.global().setProperty(runtime, "Intl", intl);
}

} // namespace react_native_linux
