#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <jsi/jsi.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

using SharedObjectInitializer = std::function<std::shared_ptr<facebook::jsi::NativeState>(
    facebook::jsi::Runtime& runtime, const facebook::jsi::Value* arguments, size_t count)>;
using SharedObjectMembers = std::vector<std::pair<std::string, facebook::jsi::HostFunctionType>>;

/**
 * A native class upstream Expo JavaScript can extend, as `expo-file-system`'s `class File extends
 * ExpoFileSystem.FileSystemFile` does (#663, ADR-0004). A module hands it out from `getConstants()`, which
 * `createTurboModuleToExpoProxy` copies onto the module object.
 *
 * The class extends `globalThis.expo.SharedObject`, so it is read when this is called: after `expo-modules-core`
 * installed its polyfill. `new` (or a subclass's `super(...)`) builds the instance from `new.target`, so it has the
 * subclass's prototype, and `initialize` gives it the native state that carries the instance's identity across the
 * boundary: a native method resolves an instance passed back to it with `sharedObjectState`. `methods` and
 * `getters` go on the prototype, with `release()`, which drops the state and so frees it now rather than when the
 * instance is collected; a later call on the instance throws.
 *
 * Threading contract: the JavaScript thread, like every JSI call. A state never released is destroyed on Hermes' JSI
 * finalizer thread once the instance is collected, so its destructor touches nothing owned by the JavaScript thread.
 */
facebook::jsi::Function makeSharedObjectClass(facebook::jsi::Runtime& runtime, SharedObjectInitializer initialize,
                                              const SharedObjectMembers& methods, const SharedObjectMembers& getters);

/**
 * What a shared object instance carries: the state `initialize` gave it, which `release()` resets. Hermes frees a
 * replaced native state only when it collects the instance, so `release()` cannot free the state by replacing it.
 */
struct SharedObjectHandle final : facebook::jsi::NativeState {
    explicit SharedObjectHandle(std::shared_ptr<facebook::jsi::NativeState> state) : state(std::move(state)) {}

    std::shared_ptr<facebook::jsi::NativeState> state;
};

/** Bytes handed to JavaScript without a copy, owned by the `ArrayBuffer` over them. */
class ByteBuffer final : public facebook::jsi::MutableBuffer {
public:
    explicit ByteBuffer(std::string bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] size_t size() const override { return bytes_.size(); }

    uint8_t* data() override { return reinterpret_cast<uint8_t*>(bytes_.data()); }

private:
    std::string bytes_;
};

/** The state `initialize` gave `value`; throws when it is not such an instance, or was released. */
template <typename State>
std::shared_ptr<State> sharedObjectState(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& value) {
    std::shared_ptr<State> state =
        value.isObject() && value.getObject(runtime).hasNativeState<SharedObjectHandle>(runtime)
            ? std::dynamic_pointer_cast<State>(
                  value.getObject(runtime).getNativeState<SharedObjectHandle>(runtime)->state)
            : nullptr;

    if (state == nullptr) {
        throw std::invalid_argument("not a live native shared object: released, or of another class");
    }

    return state;
}

} // namespace react_native_linux
