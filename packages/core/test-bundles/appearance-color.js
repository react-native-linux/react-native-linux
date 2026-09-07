// The picture half of issue #52: `PlatformColor` re-resolved across a colour-scheme change, in one bundle.
//
// hello_react --appearance-golden packages/core/test-bundles/appearance-color.js appearance-light.png light
// hello_react --appearance-golden packages/core/test-bundles/appearance-color.js appearance-dark.png dark
// hello_react --appearance-golden packages/core/test-bundles/appearance-color.js appearance-override.png dark light
//
// Three bands, top to bottom, each a window background carrying a card with the four foreground tokens as
// swatches. Every band resolves the same six `PlatformColor` names; what differs is when.
//
//   band 1 — as the run booted: the portal's scheme, unless the run was launched with an override.
//   band 2 — after this bundle overrides to dark. Dark in all three renders, which is what makes it the control.
//   band 3 — after this bundle clears the override with `auto`.
//
// Band 3 is the assertion the other two cannot make. Clearing an override resolves back to what the *portal*
// said, not to whatever the app last displayed, so a run launched with portal dark and an override of light
// draws band 1 light and band 3 dark — while a run launched in portal light draws both light. The two pictures
// differ in exactly one band, and that band is `shouldEmitOnPortalChange`'s "the portal value is still recorded"
// contract in pixels. A resolved colour cached anywhere between the scheme and the scene would make all three
// bands equal, which is the whole rn-macos bug cluster this issue was filed for.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const turboModuleProxy = globalThis.__turboModuleProxy;
const appearance =
  typeof turboModuleProxy === 'function' ? turboModuleProxy('Appearance') : globalThis.nativeModuleProxy.Appearance;

if (appearance === null || appearance === undefined) {
  throw new Error('the Appearance TurboModule was not registered');
}

const resolve = (name) => {
  const resolved = globalThis.__rnlPlatformColor(name);

  if (resolved === undefined) {
    throw new Error('PlatformColor is not supported on linux: ' + name);
  }

  return resolved;
};

const foregroundNames = ['labelColor', 'secondaryLabelColor', 'separatorColor', 'linkColor'];

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const instanceHandles = [];
let nextTag = 2;

const createNode = (componentName, props) => {
  const instanceHandle = {};
  const tag = nextTag;

  nextTag += 2;
  instanceHandles.push(instanceHandle);

  return fabric.createNode(tag, componentName, surfaceId, props, instanceHandle);
};

const createBand = () => {
  const band = createNode('View', { flex: 1, padding: 16, backgroundColor: resolve('windowBackgroundColor') });
  const card = createNode('View', {
    flex: 1,
    flexDirection: 'row',
    padding: 12,
    backgroundColor: resolve('controlBackgroundColor'),
  });

  for (const foregroundName of foregroundNames) {
    fabric.appendChild(card, createNode('View', { flex: 1, marginRight: 12, backgroundColor: resolve(foregroundName) }));
  }

  fabric.appendChild(band, card);

  return band;
};

const root = createNode('View', { flex: 1 });

console.log('appearance-color: booted ' + appearance.getColorScheme());
fabric.appendChild(root, createBand());

appearance.setColorScheme('dark');
console.log('appearance-color: overridden ' + appearance.getColorScheme());
fabric.appendChild(root, createBand());

appearance.setColorScheme('auto');
console.log('appearance-color: cleared ' + appearance.getColorScheme());
fabric.appendChild(root, createBand());

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);
