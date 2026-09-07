// The fixture #264 is graded against: one node mounted with accessibilityState.checked false, then toggled to
// true from a setTimeout so the toggle arrives as its own mounting transaction — an Update on the same tag,
// never a Remove/Insert — exactly like damage.js's moved box does for layout. ListAccessibilityChanges is asked
// only after the timer has had time to fire.
//
// rnl_window --automation --fabric packages/core/test-bundles/accessibility-state-update.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so React retains them on its fibers. Keep them alive for the same reason.
const handles = [{}, {}];

const uncheckedProps = {
  position: 'absolute',
  left: 20,
  top: 20,
  width: 60,
  height: 30,
  backgroundColor: 0xff3366cc | 0,
  testID: 'toggle',
  accessible: true,
  accessibilityRole: 'switch',
  accessibilityState: { checked: false },
};

const toggle = fabric.createNode(2, 'View', surfaceId, uncheckedProps, handles[0]);
const container = fabric.createNode(3, 'View', surfaceId, { flex: 1 }, handles[1]);

fabric.appendChild(container, toggle);

const firstChildren = fabric.createChildSet();

fabric.appendChildToSet(firstChildren, container);
fabric.completeRoot(surfaceId, firstChildren);

console.log('accessibility-state-update: committed surface ' + surfaceId);

setTimeout(() => {
  const toggledOn = fabric.cloneNodeWithNewProps(toggle, {
    ...uncheckedProps,
    accessibilityState: { checked: true },
  });
  const nextContainer = fabric.cloneNodeWithNewChildren(container);

  fabric.appendChild(nextContainer, toggledOn);

  const secondChildren = fabric.createChildSet();

  fabric.appendChildToSet(secondChildren, nextContainer);
  fabric.completeRoot(surfaceId, secondChildren);

  globalThis.__rnlMarkTestPassed();
  console.log('accessibility-state-update: toggled checked state');
}, 300);
