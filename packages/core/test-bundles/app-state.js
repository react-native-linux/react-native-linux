// The AppState fixture, issue #765. The window's activation is the only input: --inject-window-sequence replays a
// deactivation, a suspension, a resume and a reactivation, which cage never does to its only window, and this prints
// each `appStateDidChange` and `appStateFocusChange` the AppState module emits for them, the two events AppState.js
// turns into `change` and `focus`/`blur`. cage activates the window only after it maps, so the run also starts
// with a real `inactive` to `active` change; the sequence opens with three `{Activated}` steps, which change nothing
// AppState reports, so that one has landed before the window first loses activation.
//
// There is no React Native JavaScript in a bare bundle, so this reads the module the way TurboModuleRegistry does and
// answers the emitter the way RCTDeviceEventEmitter does.
//
// rnl_window --inject-window-sequence '{Deactivated}' --fabric packages/core/test-bundles/app-state.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;
const turboModuleProxy = globalThis.__turboModuleProxy;
const appState =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('AppState') : globalThis.nativeModuleProxy.AppState;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

if (appState === null || appState === undefined) {
  throw new Error('the AppState TurboModule was not registered');
}

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    if (eventName === 'appStateDidChange') {
      console.log('app-state: change ' + payload.app_state);
    } else if (eventName === 'appStateFocusChange') {
      console.log('app-state: focus ' + payload);
    }
  },
};

// C++ holds instance handles weakly, so the fixture is what keeps this one alive.
const handle = { stateNode: { node: null } };

handle.stateNode.node = fabric.createNode(2, 'View', surfaceId, { flex: 1, backgroundColor: 0xff11141a | 0 }, handle);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, handle.stateNode.node);
fabric.completeRoot(surfaceId, rootChildren);

console.log('app-state: committed surface ' + surfaceId);
