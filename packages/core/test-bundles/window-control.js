// The window-control fixture, issue #430. A tree whose layout is a pure function of the surface width — two rows
// at 50% and 100% — each reporting what a configure did to it through onLayout.
//
// Driven by --inject-window-sequence, so every line it prints is answering a replayed xdg_toplevel.configure. The
// native side prints one `[rnl-geometry] configure ...` per replayed configure, and this prints what that
// configure did to the tree, which is what makes "one relayout per configure" readable from the trace alone.
//
// hello_react --fabric packages/core/test-bundles/window-control.js

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// C++ holds instance handles weakly, so the fixture is what keeps them alive; the name rides on the handle
// because the handle is what an onLayout payload comes back with.
const handles = [];

const createView = (name, props) => {
  const handle = { name: name, stateNode: { node: null } };
  const node = fabric.createNode(handles.length + 2, 'View', surfaceId, props, handle);

  handle.stateNode.node = node;
  handles.push(handle);

  return node;
};

const row = (name, width, backgroundColor) =>
  createView(name, { width: width, height: 120, backgroundColor: backgroundColor, onLayout: true });

const half = row('half', '50%', 0xff61afef | 0);
const full = row('full', '100%', 0xffe06c75 | 0);
const surface = createView('surface', { flex: 1, backgroundColor: 0xff11141a | 0 });

fabric.appendChild(surface, half);
fabric.appendChild(surface, full);

fabric.registerEventHandler((instanceHandle, type, payload) => {
  if (type !== 'topLayout') {
    return;
  }

  console.log(
    'window-control: topLayout on ' + instanceHandle.name + ' ' + Math.round(payload.layout.width) + 'x' +
      Math.round(payload.layout.height),
  );
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('window-control: committed surface ' + surfaceId);
