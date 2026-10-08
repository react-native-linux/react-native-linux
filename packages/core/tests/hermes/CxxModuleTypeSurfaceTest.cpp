#include <NativeCxxModuleExample/NativeCxxModuleExample.h>
#include <ReactCommon/TurboModuleTestFixture.h>
#include <folly/json.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

/**
 * Issue #85: every method of rn-tester's NativeCxxModuleExample called from JavaScript, so each type in its spec
 * crosses JSI in both directions through the code @react-native/codegen generated for it — upstream's own suite
 * beside this one calls the C++ methods directly and never does. Left out: the two ArrayBuffer methods that settle
 * on a detached thread, which TestCallInvoker's unsynchronised queue cannot host, and the three that
 * react_native_assert, which abort a debug build by design.
 */
constexpr const char* kTypeSurfaceScript = R"JS(
const m = nativeModule;
const results = {};
const record = (label) => (value) => { results[label] = value; };
const message = (call) => { try { call(); return 'did not throw'; } catch (error) { return error.message; } };

results.bool = [m.getBool(true), m.getBool(false)];
results.number = m.getNumber(1.5);
results.string = m.getString('ünïcödé');
results.constants = m.getConstants();
results.customEnum = m.getCustomEnum(42);
results.numEnum = m.getNumEnum(23);
results.strEnum = m.getStrEnum('NA');
results.array = m.getArray([{a: 1, b: 'x'}, null]);
results.map = m.getMap({one: 1, none: null});
results.set = m.getSet([3, 1, 3]);
results.object = m.getObject({a: 2, b: 'y', c: 'z'});
results.value = m.getValue(2.5, 'w', {a: 3, b: 'v'});
results.union = [m.getUnion(1.44, 'Two', {value: 7}), m.getUnion(5.76, 'One', {low: 'deep'})];
results.tree = m.getBinaryTreeNode({left: {value: 2}, value: 4, right: {value: 6}});
results.graph = m.getGraphNode({label: 'root', neighbors: [{label: 'child'}]});
results.hostObject = m.consumeCustomHostObject(m.getCustomHostObject());
results.arrayBuffer = Array.from(new Uint8Array(m.getArrayBuffer(new Uint8Array([1, 2, 3]).buffer)));
results.nativeBuffer = Array.from(new Uint8Array(m.createNativeBuffer(4)));
results.optional = [m.getWithWithOptionalArgs() ?? 'absent', m.getWithWithOptionalArgs(true)];
results.voidFuncThrows = message(() => m.voidFuncThrows());
results.getObjectThrows = message(() => m.getObjectThrows({a: 1, b: 'x'}));
results.missingArgument = message(() => m.getBool());

m.getValueWithCallback(record('callback'));
m.setValueCallbackWithSubscription(record('subscription'))();
m.getValueWithPromise(false).then(record('promise'));
m.getValueWithPromise(true).catch((error) => record('rejection')(error.message));
m.voidPromise().then(() => record('voidPromise')('resolved'));

results.menu = [];
m.setMenu({
  label: 'File',
  onPress: (value, flag) => results.menu.push([value, flag]),
  items: [{label: 'Open', onPress: (value, flag) => results.menu.push([value, flag])}],
});

m.onPress(() => record('onPress')('pressed'));
m.onClick(record('onClick'));
m.onChange(record('onChange'));
m.onSubmit(record('onSubmit'));
m.onEvent(record('onEvent'));
m.voidFunc();

globalThis.__rctDeviceEventEmitter = {emit: (name, payload) => record('deviceEvent')([name, payload])};
m.emitCustomDeviceEvent('custom');

globalThis.report = () => JSON.stringify(results);
)JS";

constexpr const char* kExpectedResults = R"JSON({
  "bool": [true, false],
  "number": 1.5,
  "string": "ünïcödé",
  "constants": {"const1": true, "const2": 69, "const3": "react-native"},
  "customEnum": 42,
  "numEnum": 23,
  "strEnum": "s---b",
  "array": [{"a": 1, "b": "x"}, null],
  "map": {"none": null, "one": 1},
  "set": [1, 3],
  "object": {"a": 2, "b": "y", "c": "z"},
  "value": {"x": 2.5, "y": "w", "z": {"a": 3, "b": "v"}},
  "union": ["x: 1.44, y: Two, z: { value: 7 }", "x: 5.76, y: One, z: { low: deep }"],
  "tree": {"left": {"value": 2}, "value": 4, "right": {"value": 6}},
  "graph": {"label": "root", "neighbors": [{"label": "child"}, {"label": "top"}, {"label": "down"}]},
  "hostObject": "answer42",
  "arrayBuffer": [1, 2, 3],
  "nativeBuffer": [1, 2, 3, 4],
  "optional": ["absent", true],
  "voidFuncThrows": "Exception in HostFunction: Intentional exception from Cxx voidFuncThrows",
  "getObjectThrows": "Exception in HostFunction: Intentional exception from Cxx getObjectThrows",
  "missingArgument": "Expected argument in position 0 to be passed",
  "callback": "value from callback!",
  "subscription": "value from callback on clean up!",
  "promise": "result!",
  "rejection": "intentional promise rejection",
  "voidPromise": "resolved",
  "menu": [["value", true], ["another value", false]],
  "onPress": "pressed",
  "onClick": "value from callback on click!",
  "onChange": {"a": 1, "b": "two"},
  "onSubmit": [{"a": 1, "b": "two"}, {"a": 3, "b": "four"}, {"a": 5, "b": "six"}],
  "onEvent": "NA",
  "deviceEvent": ["custom", [true, 42, "stringArg", {"type": "one", "level": 2}]]
})JSON";

class CxxModuleTypeSurfaceTest
    : public facebook::react::TurboModuleTestFixture<facebook::react::NativeCxxModuleExample> {
protected:
    folly::dynamic run(const std::string& script) {
        facebook::jsi::Runtime& runtime = *runtime_;

        runtime.global().setProperty(runtime, "nativeModule",
                                     facebook::jsi::Object::createFromHostObject(runtime, module_));
        runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(script), "type-surface.js");
        jsInvoker_->flushQueue();

        return folly::parseJson(
            runtime.global().getPropertyAsFunction(runtime, "report").call(runtime).getString(runtime).utf8(runtime));
    }
};

TEST_F(CxxModuleTypeSurfaceTest, EveryTypeInTheSpecRoundTripsThroughJsi) {
    EXPECT_EQ(run(kTypeSurfaceScript), folly::parseJson(kExpectedResults));
}

} // namespace
} // namespace react_native_linux
