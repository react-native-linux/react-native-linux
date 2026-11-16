// The AppState fixture, issue #765. The window's state is the only input: --inject-window-sequence replays a
// deactivation, a suspension, a resume and a reactivation, which cage never does to its only window, and this prints
// each `appStateDidChange` and `appStateFocusChange` the AppState module emits for them, the two events AppState.js
// turns into `change` and `focus`/`blur`. Losing activation is `blur` alone, never a change: a desktop window is
// `active` until the compositor suspends it.
//
// Change events are numbered, so `change 1` being the suspension proves the session started with no change of its
// own: `WindowSession` seeds the window's state before this bundle runs. The sequence opens with three `{Activated}`
// steps, which change nothing AppState reports, so cage's own late activation has landed before the window first
// loses it.
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

let changeCount = 0;

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    if (eventName === 'appStateDidChange') {
      changeCount += 1;
      console.log('app-state: change ' + changeCount + ' ' + payload.app_state);
    } else if (eventName === 'appStateFocusChange') {
      console.log('app-state: focus ' + payload);
    }
  },
};

console.log('app-state: initial ' + appState.getConstants().initialAppState);

// C++ holds instance handles weakly, so the fixture is what keeps this one alive.
const handle = { stateNode: { node: null } };

handle.stateNode.node = fabric.createNode(2, 'View', surfaceId, { flex: 1, backgroundColor: 0xff11141a | 0 }, handle);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, handle.stateNode.node);
fabric.completeRoot(surfaceId, rootChildren);

console.log('app-state: committed surface ' + surfaceId);
