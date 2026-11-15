#include "ExpoSharedObject.h"

namespace react_native_linux {

namespace {

// Hermes cannot `new` a host function, so the constructor is JavaScript that hands the host function the instance.
constexpr char kSharedObjectClassSource[] = R"JAVASCRIPT(
(initialize, methods, getters) => {
  const { SharedObject } = globalThis.expo;
  function NativeSharedObject(...args) {
    const self = Reflect.construct(SharedObject, [], new.target);
    initialize(self, ...args);
    return self;
  }
  Object.setPrototypeOf(NativeSharedObject, SharedObject);
  Object.setPrototypeOf(NativeSharedObject.prototype, SharedObject.prototype);
  Object.assign(NativeSharedObject.prototype, methods);
  for (const name of Object.keys(getters)) {
    Object.defineProperty(NativeSharedObject.prototype, name, { get: getters[name], configurable: true });
  }
  return NativeSharedObject;
}
)JAVASCRIPT";

facebook::jsi::Object hostFunctions(facebook::jsi::Runtime& runtime, const SharedObjectMembers& members) {
    facebook::jsi::Object object(runtime);

    for (const auto& [name, function] : members) {
        object.setProperty(runtime, name.c_str(),
                           facebook::jsi::Function::createFromHostFunction(
                               runtime, facebook::jsi::PropNameID::forUtf8(runtime, name), 0, function));
    }

    return object;
}

} // namespace

facebook::jsi::Function makeSharedObjectClass(facebook::jsi::Runtime& runtime, SharedObjectInitializer initialize,
                                              const SharedObjectMembers& methods, const SharedObjectMembers& getters) {
    SharedObjectMembers methodsWithRelease = methods;

    methodsWithRelease.emplace_back("release",
                                    [](facebook::jsi::Runtime& releaseRuntime, const facebook::jsi::Value& thisValue,
                                       const facebook::jsi::Value*, size_t) {
                                        const facebook::jsi::Object instance = thisValue.asObject(releaseRuntime);

                                        if (instance.hasNativeState<SharedObjectHandle>(releaseRuntime)) {
                                            instance.getNativeState<SharedObjectHandle>(releaseRuntime)->state.reset();
                                        }

                                        return facebook::jsi::Value::undefined();
                                    });

    const facebook::jsi::Function initializeFunction = facebook::jsi::Function::createFromHostFunction(
        runtime, facebook::jsi::PropNameID::forAscii(runtime, "initialize"), 1,
        [initialize = std::move(initialize)](facebook::jsi::Runtime& constructorRuntime, const facebook::jsi::Value&,
                                             const facebook::jsi::Value* arguments, size_t count) {
            arguments[0]
                .asObject(constructorRuntime)
                .setNativeState(constructorRuntime, std::make_shared<SharedObjectHandle>(
                                                        initialize(constructorRuntime, arguments + 1, count - 1)));

            return facebook::jsi::Value::undefined();
        });

    return runtime
        .evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(kSharedObjectClassSource),
                            "ExpoSharedObject.js")
        .asObject(runtime)
        .asFunction(runtime)
        .call(runtime, initializeFunction, hostFunctions(runtime, methodsWithRelease), hostFunctions(runtime, getters))
        .asObject(runtime)
        .asFunction(runtime);
}

} // namespace react_native_linux
