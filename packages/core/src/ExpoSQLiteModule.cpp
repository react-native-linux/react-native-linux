#include "ExpoSQLite.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <functional>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

template <> struct facebook::react::Bridging<react_native_linux::SQLiteValue> {
    static jsi::Value toJs(jsi::Runtime& runtime, const react_native_linux::SQLiteValue& value) {
        if (const double* number = std::get_if<double>(&value)) {
            return *number;
        }

        if (const std::string* text = std::get_if<std::string>(&value)) {
            return jsi::String::createFromUtf8(runtime, *text);
        }

        return jsi::Value::null();
    }
};

template <> struct facebook::react::Bridging<react_native_linux::SQLiteRunResult> {
    static jsi::Object toJs(jsi::Runtime& runtime, const react_native_linux::SQLiteRunResult& result) {
        jsi::Object object(runtime);

        object.setProperty(runtime, "lastInsertRowId", result.lastInsertRowId);
        object.setProperty(runtime, "changes", result.changes);
        object.setProperty(runtime, "firstRowValues", bridging::toJs(runtime, result.firstRowValues, nullptr));

        return object;
    }
};

namespace react_native_linux {

namespace {

/**
 * `ExpoSQLite`, the native module upstream expo-sqlite's JavaScript requires (#638), registered as a TurboModule so
 * expo-modules-core's `requireNativeModule` reaches it through `createTurboModuleToExpoProxy`, which copies the
 * methods and spreads `getConstants()` onto the module object. Upstream's JavaScript then runs unmodified over it.
 * The constants are `defaultDatabaseDirectory`, `bundledExtensions` (none) and the two classes T3 Code's calls
 * reach, `NativeDatabase` and `NativeStatement`, with the asynchronous half of their upstream surface. On Android and
 * iOS those are Expo `SharedObject`s; nothing in expo-sqlite's JavaScript uses the `SharedObject` API, so here each
 * instance is a plain object carrying its `SQLiteDatabases` handle as JSI native state, with its methods on the
 * class prototype. The synchronous half, sessions, serialization, extensions, backups, deletion and the change
 * listener are absent, so a call to one fails as a call to an undefined function.
 *
 * Threading contract: every method reads its arguments on the JavaScript thread and posts the database work to
 * `SQLiteDatabases`' worker, which settles the `AsyncPromise` it was handed back through the call invoker (#77). The
 * functions hold the databases by reference because `TurboModuleRegistry` owns them and outlives every JavaScript
 * call: `ReactHost` destroys it only after its JavaScript thread has quit.
 */
class LinuxSQLiteModule final : public facebook::react::TurboModule {
public:
    LinuxSQLiteModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, SQLiteDatabases& databases)
        : TurboModule("ExpoSQLite", std::move(jsInvoker)), databases_(databases) {
        methodMap_["getConstants"] = {0, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                                          return static_cast<LinuxSQLiteModule&>(turboModule).constants(runtime);
                                      }};
        methodMap_["ensureDatabasePathExistsAsync"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                auto& module = static_cast<LinuxSQLiteModule&>(turboModule);

                return schedule<>(runtime, module.context(),
                                  [databasePath = text(runtime, arguments, count, 0)](SQLiteDatabases&) {
                                      SQLiteDatabases::ensureDirectoryExists(databasePath);
                                  });
            }};
    }

private:
    using Runtime = facebook::jsi::Runtime;
    using Value = facebook::jsi::Value;

    /** What upstream's `NativeDatabase` and `NativeStatement` instances carry. */
    struct Handle final : facebook::jsi::NativeState {
        Handle(int handle, std::string databasePath) : handle(handle), databasePath(std::move(databasePath)) {}

        int handle;
        std::string databasePath;
    };

    struct Context {
        std::shared_ptr<facebook::react::CallInvoker> jsInvoker;
        SQLiteDatabases& databases;
    };

    using Method = std::function<Value(Runtime&, const Context&, const Handle&, const Value*, size_t)>;

    Context context() { return {jsInvoker_, databases_}; }

    Value constants(Runtime& runtime) {
        facebook::jsi::Object constants(runtime);
        const std::string& directory = databases_.directory();

        constants.setProperty(runtime, "defaultDatabaseDirectory",
                              directory.empty() ? Value::null()
                                                : Value(facebook::jsi::String::createFromUtf8(runtime, directory)));
        constants.setProperty(runtime, "bundledExtensions", facebook::jsi::Object(runtime));
        constants.setProperty(runtime, "NativeDatabase",
                              makeClass(runtime, "NativeDatabase", databaseMethods(), databasePathOf));
        constants.setProperty(runtime, "NativeStatement",
                              makeClass(runtime, "NativeStatement", statementMethods(),
                                        [](Runtime&, const Value*, size_t) { return std::string(); }));

        return constants;
    }

    /**
     * `new NativeDatabase(databasePath, options, serializedData)`. `deserializeDatabaseAsync` passes the bytes as
     * `serializedData`, and an open option other than `useNewConnection` and `finalizeUnusedStatementsBeforeClosing`
     * asks for behavior this module does not have, so either fails rather than opening a database without it.
     */
    static std::string databasePathOf(Runtime& runtime, const Value* arguments, size_t count) {
        if (count > 2 && !arguments[2].isUndefined()) {
            throw std::invalid_argument("ExpoSQLite: deserializing a database is not supported on Linux");
        }

        if (count > 1 && arguments[1].isObject()) {
            const facebook::jsi::Object options = arguments[1].getObject(runtime);
            const facebook::jsi::Array names = options.getPropertyNames(runtime);

            for (size_t index = 0; index < names.size(runtime); ++index) {
                const std::string name = names.getValueAtIndex(runtime, index).asString(runtime).utf8(runtime);
                const Value value = options.getProperty(runtime, name.c_str());
                const bool enabled = value.isBool() ? value.getBool() : !value.isUndefined() && !value.isNull();

                if (enabled && name != "useNewConnection" && name != "finalizeUnusedStatementsBeforeClosing") {
                    throw std::invalid_argument("ExpoSQLite: the open option " + name + " is not supported on Linux");
                }
            }
        }

        return text(runtime, arguments, count, 0);
    }

    /**
     * A constructor whose instances share `methods` through its prototype. `new` hands the constructor an instance
     * of that prototype as `this`, which gets a fresh handle and, for a database, the path to open.
     */
    facebook::jsi::Function makeClass(Runtime& runtime, const char* name,
                                      const std::vector<std::pair<const char*, Method>>& methods,
                                      std::function<std::string(Runtime&, const Value*, size_t)> databasePathOf) {
        facebook::jsi::Object prototype(runtime);

        for (const auto& [methodName, method] : methods) {
            prototype.setProperty(runtime, methodName,
                                  facebook::jsi::Function::createFromHostFunction(
                                      runtime, facebook::jsi::PropNameID::forAscii(runtime, methodName), 0,
                                      [method, context = context()](Runtime& methodRuntime, const Value& thisValue,
                                                                    const Value* arguments, size_t count) {
                                          return method(methodRuntime, context, *handleOf(methodRuntime, thisValue),
                                                        arguments, count);
                                      }));
        }

        const facebook::jsi::Function initialize = facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, name), 1,
            [&databases = databases_, databasePathOf = std::move(databasePathOf)](
                Runtime& constructorRuntime, const Value&, const Value* arguments, size_t count) {
                argument(arguments, count, 0)
                    .asObject(constructorRuntime)
                    .setNativeState(constructorRuntime,
                                    std::make_shared<Handle>(
                                        databases.reserveHandle(),
                                        databasePathOf(constructorRuntime, arguments + 1, count == 0 ? 0 : count - 1)));

                return Value::undefined();
            });
        // Hermes cannot `new` a host function, so the constructor is a JavaScript function that hands it `this`.
        facebook::jsi::Function constructor =
            runtime
                .evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(
                                        "(initialize) => function () { initialize(this, ...arguments); }"),
                                    "ExpoSQLite.js")
                .asObject(runtime)
                .asFunction(runtime)
                .call(runtime, initialize)
                .asObject(runtime)
                .asFunction(runtime);

        constructor.setProperty(runtime, "prototype", prototype);

        return constructor;
    }

    static std::vector<std::pair<const char*, Method>> databaseMethods() {
        return {
            {"initAsync",
             [](Runtime& runtime, const Context& context, const Handle& database, const Value*, size_t) {
                 return schedule<>(runtime, context,
                                   [handle = database.handle, databasePath = database.databasePath](
                                       SQLiteDatabases& databases) { databases.open(handle, databasePath); });
             }},
            {"isInTransactionAsync",
             [](Runtime& runtime, const Context& context, const Handle& database, const Value*, size_t) {
                 return schedule<bool>(runtime, context, [handle = database.handle](SQLiteDatabases& databases) {
                     return databases.isInTransaction(handle);
                 });
             }},
            {"closeAsync",
             [](Runtime& runtime, const Context& context, const Handle& database, const Value*, size_t) {
                 return schedule<>(runtime, context,
                                   [handle = database.handle](SQLiteDatabases& databases) { databases.close(handle); });
             }},
            {"execAsync",
             [](Runtime& runtime, const Context& context, const Handle& database, const Value* arguments,
                size_t count) {
                 return schedule<>(runtime, context,
                                   [handle = database.handle, sql = text(runtime, arguments, count, 0)](
                                       SQLiteDatabases& databases) { databases.exec(handle, sql); });
             }},
            {"prepareAsync",
             [](Runtime& runtime, const Context& context, const Handle& database, const Value* arguments,
                size_t count) {
                 return schedule<>(runtime, context,
                                   [handle = database.handle,
                                    statement = handleOf(runtime, argument(arguments, count, 0))->handle,
                                    sql = text(runtime, arguments, count, 1)](SQLiteDatabases& databases) {
                                       databases.prepare(handle, statement, sql);
                                   });
             }},
        };
    }

    static std::vector<std::pair<const char*, Method>> statementMethods() {
        return {
            {"runAsync",
             [](Runtime& runtime, const Context& context, const Handle& statement, const Value* arguments,
                size_t count) {
                 return schedule<SQLiteRunResult>(
                     runtime, context,
                     [handle = statement.handle, parameters = parametersOf(runtime, arguments, count)](
                         SQLiteDatabases& databases) { return databases.run(handle, parameters); });
             }},
            {"stepAsync",
             [](Runtime& runtime, const Context& context, const Handle& statement, const Value*, size_t) {
                 return schedule<std::optional<SQLiteRow>>(
                     runtime, context,
                     [handle = statement.handle](SQLiteDatabases& databases) { return databases.step(handle); });
             }},
            {"getAllAsync",
             [](Runtime& runtime, const Context& context, const Handle& statement, const Value*, size_t) {
                 return schedule<std::vector<SQLiteRow>>(
                     runtime, context,
                     [handle = statement.handle](SQLiteDatabases& databases) { return databases.stepAll(handle); });
             }},
            {"resetAsync",
             [](Runtime& runtime, const Context& context, const Handle& statement, const Value*, size_t) {
                 return schedule<>(runtime, context, [handle = statement.handle](SQLiteDatabases& databases) {
                     databases.reset(handle);
                 });
             }},
            {"getColumnNamesAsync",
             [](Runtime& runtime, const Context& context, const Handle& statement, const Value*, size_t) {
                 return schedule<std::vector<std::string>>(
                     runtime, context,
                     [handle = statement.handle](SQLiteDatabases& databases) { return databases.columnNames(handle); });
             }},
            {"finalizeAsync",
             [](Runtime& runtime, const Context& context, const Handle& statement, const Value*, size_t) {
                 return schedule<>(runtime, context, [handle = statement.handle](SQLiteDatabases& databases) {
                     databases.finalize(handle);
                 });
             }},
        };
    }

    template <typename... Result, typename Work>
    static Value schedule(Runtime& runtime, const Context& context, Work work) {
        facebook::react::AsyncPromise<Result...> promise(runtime, context.jsInvoker);
        Value promiseObject = promise.get(runtime);

        context.databases.post(
            [promise = std::move(promise), work = std::move(work)](SQLiteDatabases& databases) mutable {
                try {
                    if constexpr (sizeof...(Result) == 0) {
                        work(databases);
                        promise.resolve();
                    } else {
                        promise.resolve(work(databases));
                    }
                } catch (const std::exception& error) {
                    promise.reject(facebook::react::Error(error.what()));
                }
            });

        return promiseObject;
    }

    static const Value& argument(const Value* arguments, size_t count, size_t index) {
        if (index >= count) {
            throw std::invalid_argument("ExpoSQLite: argument " + std::to_string(index) + " is missing");
        }

        return arguments[index];
    }

    static std::string text(Runtime& runtime, const Value* arguments, size_t count, size_t index) {
        return argument(arguments, count, index).asString(runtime).utf8(runtime);
    }

    static std::shared_ptr<Handle> handleOf(Runtime& runtime, const Value& value) {
        const facebook::jsi::Object object = value.asObject(runtime);

        if (!object.hasNativeState<Handle>(runtime)) {
            throw std::invalid_argument("ExpoSQLite: not a NativeDatabase or a NativeStatement");
        }

        return object.getNativeState<Handle>(runtime);
    }

    /** `runAsync(database, bindParams, bindBlobParams, shouldPassAsArray)`, as upstream's `normalizeParams` builds it.
     */
    static SQLiteBindParameters parametersOf(Runtime& runtime, const Value* arguments, size_t count) {
        const facebook::jsi::Object values = argument(arguments, count, 1).asObject(runtime);
        const facebook::jsi::Array names = values.getPropertyNames(runtime);
        SQLiteBindParameters parameters;

        if (argument(arguments, count, 2).asObject(runtime).getPropertyNames(runtime).size(runtime) > 0) {
            throw std::invalid_argument("ExpoSQLite: BLOB parameters are not supported on Linux");
        }

        parameters.byIndex = argument(arguments, count, 3).asBool();

        for (size_t index = 0; index < names.size(runtime); ++index) {
            const std::string name = names.getValueAtIndex(runtime, index).asString(runtime).utf8(runtime);
            const Value value = values.getProperty(runtime, name.c_str());

            if (value.isNumber()) {
                parameters.values.emplace_back(name, value.getNumber());
            } else if (value.isString()) {
                parameters.values.emplace_back(name, value.getString(runtime).utf8(runtime));
            } else if (value.isNull()) {
                parameters.values.emplace_back(name, nullptr);
            } else {
                throw std::invalid_argument("ExpoSQLite: parameter " + name + " is not a string, a number or null");
            }
        }

        return parameters;
    }

    SQLiteDatabases& databases_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoSQLiteModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, SQLiteDatabases& databases) {
    return std::make_shared<LinuxSQLiteModule>(std::move(jsInvoker), databases);
}

} // namespace react_native_linux
