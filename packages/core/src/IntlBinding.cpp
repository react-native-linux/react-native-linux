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
using BoundMethod = std::function<jsi::Value(jsi::Runtime&, const Format&, const jsi::Value*, size_t)>;

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

double toNumber(jsi::Runtime& runtime, const jsi::Value& value) {
    return value.isNumber() ? value.getNumber() : callGlobal(runtime, "Number", value).getNumber();
}

/** ECMA-262's `ToLength`: an integer from 0 to 2^53 - 1, NaN being 0. */
std::uint64_t toLength(jsi::Runtime& runtime, const jsi::Value& value) {
    constexpr double kMaximumLength = 9'007'199'254'740'991.0;
    const double length = toNumber(runtime, value);

    return std::isnan(length) ? 0 : static_cast<std::uint64_t>(std::clamp(std::trunc(length), 0.0, kMaximumLength));
}

/** ECMA-402's `CanonicalizeLocaleList` up to the canonicalization itself: a string, or a list of strings. */
std::vector<std::string> readLocales(jsi::Runtime& runtime, const jsi::Value& locales) {
    if (locales.isUndefined()) {
        return {};
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

        tags.push_back(element.toString(runtime).utf8(runtime));
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

/**
 * One `Intl` constructor: callable with or without `new`, as ECMA-402 requires of these three, with the method
 * that `format` and `compare` are — a getter returning a function bound to the instance — and `resolvedOptions`.
 * Returns the prototype, for methods only one constructor has.
 */
template <typename Format>
jsi::Object installConstructor(jsi::Runtime& runtime, jsi::Object& intl, const std::string& name,
                               const char* boundMethodName, unsigned int boundMethodLength, Construct<Format> construct,
                               BoundMethod<Format> boundMethod) {
    const std::string qualifiedName = "Intl." + name;
    jsi::Object prototype(runtime);
    const jsi::Function create = hostFunction(
        runtime, name.c_str(), 0,
        [construct = std::move(construct)](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                           const jsi::Value* arguments, size_t count) -> jsi::Value {
            const std::vector<std::string> locales = readLocales(callRuntime, argumentAt(arguments, count, 0));
            const IntlOptions options =
                readOptions(callRuntime, argumentAt(arguments, count, 1), Format::optionNames());
            std::shared_ptr<IntlState<Format>> state = translatingErrors<std::shared_ptr<IntlState<Format>>>(
                callRuntime, [&construct, &locales, &options]() { return construct(locales, options); });
            jsi::Object instance = jsi::Object::create(callRuntime, argumentAt(arguments, count, 2));

            instance.setNativeState(callRuntime, std::move(state));

            return instance;
        });

    // Hermes refuses `new` on a JSI host function ("This function cannot be used as a constructor"), so the
    // constructor is a plain function that returns what the host function made, which `new` then yields. It hands
    // over its own `prototype`, so no host closure holds a JavaScript object.
    const jsi::Function constructor =
        runtime.global()
            .getPropertyAsFunction(runtime, "Function")
            .callAsConstructor(runtime, "create",
                               "return function " + name + "(locales, options) { return create(locales, options, " +
                                   name + ".prototype); };")
            .asObject(runtime)
            .asFunction(runtime)
            .call(runtime, create)
            .asObject(runtime)
            .asFunction(runtime);

    const std::string boundMethodQualifiedName = qualifiedName + ".prototype." + boundMethodName;
    const jsi::Function getter = hostFunction(
        runtime, boundMethodName, 0,
        [boundMethod = std::move(boundMethod), boundMethodQualifiedName, boundMethodName = std::string(boundMethodName),
         boundMethodLength](jsi::Runtime& getterRuntime, const jsi::Value& thisValue, const jsi::Value* /*arguments*/,
                            size_t /*count*/) -> jsi::Value {
            const std::shared_ptr<IntlState<Format>> state =
                stateOf<Format>(getterRuntime, thisValue, boundMethodQualifiedName);
            jsi::Function bound =
                hostFunction(getterRuntime, "", boundMethodLength,
                             [boundMethod, state](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                                  const jsi::Value* arguments, size_t count) -> jsi::Value {
                                 return translatingErrors<jsi::Value>(callRuntime, [&]() {
                                     return boundMethod(callRuntime, state->format, arguments, count);
                                 });
                             });
            jsi::Object descriptor(getterRuntime);

            // ECMA-402 keeps the bound function in an internal slot, so every read returns the same one. A
            // non-enumerable own property is that slot here: the native state cannot hold it without a cycle the
            // collector does not see. Reflect leaves a frozen instance uncached rather than throwing.
            descriptor.setProperty(getterRuntime, "value", bound);
            descriptor.setProperty(getterRuntime, "configurable", true);
            getterRuntime.global()
                .getPropertyAsObject(getterRuntime, "Reflect")
                .getPropertyAsFunction(getterRuntime, "defineProperty")
                .call(getterRuntime, thisValue, boundMethodName, descriptor);

            return bound;
        });

    jsi::Object descriptor(runtime);

    descriptor.setProperty(runtime, "get", getter);
    descriptor.setProperty(runtime, "configurable", true);
    runtime.global()
        .getPropertyAsObject(runtime, "Object")
        .getPropertyAsFunction(runtime, "defineProperty")
        .call(runtime, prototype, boundMethodName, descriptor);

    const std::string resolvedOptionsName = qualifiedName + ".prototype.resolvedOptions";

    prototype.setProperty(
        runtime, "resolvedOptions",
        hostFunction(runtime, "resolvedOptions", 0,
                     [resolvedOptionsName](jsi::Runtime& callRuntime, const jsi::Value& thisValue,
                                           const jsi::Value* /*arguments*/, size_t /*count*/) -> jsi::Value {
                         return toObject(
                             callRuntime,
                             stateOf<Format>(callRuntime, thisValue, resolvedOptionsName)->format.resolvedOptions());
                     }));
    prototype.setProperty(runtime, "constructor", constructor);
    constructor.setProperty(runtime, "prototype", prototype);
    intl.setProperty(runtime, name.c_str(), constructor);

    return prototype;
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

jsi::Array toArray(jsi::Runtime& runtime, const IntlParts& parts) {
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

    const jsi::Object dateTimeFormatPrototype = installConstructor<IntlDateTimeFormat>(
        runtime, intl, "DateTimeFormat", "format", 1,
        [](std::span<const std::string> locales, const IntlOptions& options) {
            return std::make_shared<IntlState<IntlDateTimeFormat>>(locales, options, defaults().locale,
                                                                   defaults().timeZone);
        },
        [](jsi::Runtime& runtime, const IntlDateTimeFormat& format, const jsi::Value* arguments,
           size_t count) -> jsi::Value {
            return jsi::String::createFromUtf8(runtime,
                                               format.format(timeOf(runtime, argumentAt(arguments, count, 0))));
        });

    dateTimeFormatPrototype.setProperty(
        runtime, "formatToParts",
        hostFunction(runtime, "formatToParts", 1,
                     [](jsi::Runtime& callRuntime, const jsi::Value& thisValue, const jsi::Value* arguments,
                        size_t count) -> jsi::Value {
                         const std::shared_ptr<IntlState<IntlDateTimeFormat>> state = stateOf<IntlDateTimeFormat>(
                             callRuntime, thisValue, "Intl.DateTimeFormat.prototype.formatToParts");
                         const double time = timeOf(callRuntime, argumentAt(arguments, count, 0));

                         return toArray(callRuntime, translatingErrors<IntlParts>(callRuntime, [&state, time]() {
                                            return state->format.formatToParts(time);
                                        }));
                     }));

    installConstructor<IntlNumberFormat>(
        runtime, intl, "NumberFormat", "format", 1,
        [](std::span<const std::string> locales, const IntlOptions& options) {
            return std::make_shared<IntlState<IntlNumberFormat>>(locales, options, defaults().locale);
        },
        [](jsi::Runtime& runtime, const IntlNumberFormat& format, const jsi::Value* arguments,
           size_t count) -> jsi::Value {
            return jsi::String::createFromUtf8(runtime,
                                               format.format(toNumber(runtime, argumentAt(arguments, count, 0))));
        });

    installConstructor<IntlCollator>(
        runtime, intl, "Collator", "compare", 2,
        [](std::span<const std::string> locales, const IntlOptions& options) {
            return std::make_shared<IntlState<IntlCollator>>(locales, options, defaults().locale);
        },
        [](jsi::Runtime& runtime, const IntlCollator& collator, const jsi::Value* arguments,
           size_t count) -> jsi::Value {
            return collator.compare(argumentAt(arguments, count, 0).toString(runtime).utf8(runtime),
                                    argumentAt(arguments, count, 1).toString(runtime).utf8(runtime));
        });

    intl.setProperty(runtime, "getCanonicalLocales",
                     hostFunction(runtime, "getCanonicalLocales", 1,
                                  [](jsi::Runtime& callRuntime, const jsi::Value& /*thisValue*/,
                                     const jsi::Value* arguments, size_t count) -> jsi::Value {
                                      const std::vector<std::string> canonical =
                                          translatingErrors<std::vector<std::string>>(callRuntime, [&]() {
                                              return canonicalizeLocaleList(
                                                  readLocales(callRuntime, argumentAt(arguments, count, 0)));
                                          });
                                      jsi::Array array(callRuntime, canonical.size());

                                      for (size_t index = 0; index < canonical.size(); ++index) {
                                          array.setValueAtIndex(
                                              callRuntime, index,
                                              jsi::String::createFromUtf8(callRuntime, canonical[index]));
                                      }

                                      return array;
                                  }));

    runtime.global().setProperty(runtime, "Intl", intl);
}

} // namespace react_native_linux
