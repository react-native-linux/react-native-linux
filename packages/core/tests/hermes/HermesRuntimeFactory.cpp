#include <hermes/hermes.h>
#include <jsi/test/testlib.h>

#include <memory>
#include <vector>

namespace facebook::jsi {

std::vector<RuntimeFactory> runtimeGenerators() {
    return {[]() -> std::shared_ptr<Runtime> { return facebook::hermes::makeHermesRuntime(); }};
}

} // namespace facebook::jsi
