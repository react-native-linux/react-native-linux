// The <Modal> fixture for ADR-0005's first slice (#794), committed the way Modal.js commits it: a ModalHostView at
// `position: absolute` holding a `flex: 1` container whose background is the backdrop.
//
// The modal is declared inside the worst parent a tree can give it: one that clips, is translated, rotated, scaled
// and half transparent, and sits at `zIndex: -1` under a later sibling with `zIndex: 5`. A modal that inherited
// any of that would be cut down to the parent's tilted box or painted under the sibling.
//
//   --golden         modal-backdrop.png  an opaque modal with a backdropColor: nothing beneath it shows
//   --clicked-frame  modal-scrim.png     a click on the card swaps in a transparent modal whose child View is the
//                                        scrim, so the rows and the sibling show through it, dimmed
//
// The swap removes one overlay and presents another in one commit, and the click that asks for it only reaches the
// card because the overlay is hit-tested first.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Instance handles are retained here, in the fiber shape PointerEventsProcessor resolves a target through.
const instanceHandles = [];

const view = (props, children = [], componentName = 'View') => {
  const handle = { stateNode: { node: null } };
  const created = fabric.createNode(2 * (instanceHandles.length + 1), componentName, surfaceId, props, handle);

  handle.stateNode.node = created;
  instanceHandles.push(handle);
  children.forEach((child) => fabric.appendChild(created, child));

  return created;
};

const rows = Array.from({ length: 8 }, (_, index) =>
  view({
    position: 'absolute',
    left: 40,
    top: 40 + index * 66,
    width: 520,
    height: 52,
    borderRadius: 8,
    backgroundColor: (index % 2 === 0 ? 0xff98c379 : 0xff61afef) | 0,
  }),
);

const sibling = view({
  position: 'absolute',
  left: 600,
  top: 430,
  width: 160,
  height: 120,
  zIndex: 5,
  backgroundColor: 0xffe06c75 | 0,
});

let card;

const modal = (isTransparent) => {
  card = view({
    width: 360,
    height: 200,
    borderRadius: 16,
    backgroundColor: 0xfff5f5f5 | 0,
    onClick: !isTransparent,
  });

  const scrim = isTransparent
    ? [view({ position: 'absolute', left: 0, top: 0, right: 0, bottom: 0, backgroundColor: 0x80000000 | 0 })]
    : [];
  const container = view(
    {
      flex: 1,
      top: 0,
      left: 0,
      alignItems: 'center',
      justifyContent: 'center',
      backgroundColor: (isTransparent ? 0x00000000 : 0xff2b3a55) | 0,
    },
    [...scrim, card],
  );

  return view({ position: 'absolute', transparent: isTransparent }, [container], 'ModalHostView');
};

const parentProps = {
  position: 'absolute',
  left: 590,
  top: 140,
  width: 180,
  height: 120,
  overflow: 'hidden',
  opacity: 0.5,
  zIndex: -1,
  transform: [{ translateX: 30 }, { rotate: '12deg' }, { scale: 0.6 }],
  backgroundColor: 0xffd19a66 | 0,
};

const parent = view(parentProps);
const root = view({ flex: 1 });

const commit = (presented) => {
  const nextParent = fabric.cloneNodeWithNewChildren(parent);
  const nextRoot = fabric.cloneNodeWithNewChildren(root);
  const rootChildren = fabric.createChildSet();

  fabric.appendChild(nextParent, presented);
  [...rows, nextParent, sibling].forEach((child) => fabric.appendChild(nextRoot, child));
  fabric.appendChildToSet(rootChildren, nextRoot);
  fabric.completeRoot(surfaceId, rootChildren);
};

let cardHandle;

fabric.registerEventHandler((instanceHandle, type) => {
  if (instanceHandle !== cardHandle || type !== 'topClick') {
    return;
  }

  cardHandle = undefined;
  commit(modal(true));
  console.log('modal: swapped in the transparent modal');
});

commit(modal(false));
cardHandle = instanceHandles.find((handle) => handle.stateNode.node === card);

console.log('modal: committed surface ' + surfaceId);
