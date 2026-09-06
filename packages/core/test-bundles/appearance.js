// The Appearance trace of issue #52: what `appearanceChanged` fires for, what it does not, and what
// `PlatformColor` answers on either side of it.
//
// hello_react packages/core/test-bundles/appearance.js
//
// The run starts in the light fallback, because a headless run has no session bus and therefore no portal
// (`kFallbackColorScheme`). From there the bundle overrides to dark, restates the same override, and clears it.
// The trace is the whole precedence contract of #260 read from JavaScript rather than from C++: the override
// wins while it is set, a restated override is not a change and fires nothing, and clearing resolves back to the
// portal's value. `labelColor` is resolved at each step, so the same name answering a different colour is the
// JavaScript-visible proof that nothing between the scheme and a prop caches a resolved colour.
//
// The trace is asserted from a timer rather than at module scope: `emitDeviceEvent` reaches JavaScript through
// the module's `CallInvoker`, so an event queued by a synchronous `setColorScheme` lands after the call that
// queued it returns. A fixture that read `events` immediately would be asserting on the emit being synchronous,
// which it is not and must not be.
//
// There is no React and no Appearance module in a bare bundle, so this is what those two would do: read the
// module the way TurboModuleRegistry does, and answer the emitter the way RCTDeviceEventEmitter does.

const turboModuleProxy = globalThis.__turboModuleProxy;
const appearance =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('Appearance') : globalThis.nativeModuleProxy.Appearance;

if (appearance === null || appearance === undefined) {
  throw new Error('the Appearance TurboModule was not registered');
}

const events = [];

globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, preferences) => {
    events.push(preferences.colorScheme);
    console.log('appearance: event ' + eventName + ' ' + preferences.colorScheme);
  },
};

const resolveLabelColor = () => {
  const resolved = globalThis.__rnlPlatformColor('labelColor');

  if (resolved === undefined) {
    throw new Error('PlatformColor is not supported on linux: labelColor');
  }

  return resolved;
};

appearance.addListener('appearanceChanged');

console.log('appearance: initial ' + appearance.getColorScheme());

const initialLabelColor = resolveLabelColor();

appearance.setColorScheme('dark');
console.log('appearance: after override ' + appearance.getColorScheme());

const overriddenLabelColor = resolveLabelColor();

// A restated override is not a change, so nothing is emitted for this call and the trace below is the proof.
appearance.setColorScheme('dark');

// `auto` is `ColorSchemeOverride`'s spelling of "clear", so this resolves back to the portal value the run
// started in.
appearance.setColorScheme('auto');
console.log('appearance: after clear ' + appearance.getColorScheme());

const clearedLabelColor = resolveLabelColor();

appearance.removeListeners(1);

console.log('appearance: labelColor ' + initialLabelColor + ',' + overriddenLabelColor + ',' + clearedLabelColor);
console.log(
  initialLabelColor !== overriddenLabelColor && clearedLabelColor === initialLabelColor
    ? 'appearance: platform colour ok'
    : 'appearance: platform colour stale',
);

// A name outside the token set answers `undefined` rather than a colour or an exception, which is what lets the
// JavaScript wrapper throw something a developer can read instead of letting an undefined colour reach a prop.
// rn-macos#413 is the version of this where it does not.
console.log('appearance: unsupported ' + String(globalThis.__rnlPlatformColor('systemPinkColor')));

setTimeout(() => {
  console.log('appearance: trace ' + events.join(','));
  console.log(events.join(',') === 'dark,light' ? 'appearance: trace ok' : 'appearance: trace wrong');
}, 0);
