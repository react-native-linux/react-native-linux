// The rejected-edit fixture for issue #114, item 4: a controlled digits-only field whose parent strips a
// keystroke it does not accept rather than adopting it, so the field's buffer has to fall back to what the
// parent actually approved. `EditorModel::reconcileProps` already adopts a `value` exactly when its echoed
// event count matches the buffer's, whatever the text — `ARejectedEditWithAMatchingEventCountRevertsWithoutMovingTheCount`
// in `EditorTest.cpp` is that half of the contract in isolation. This is the other half: a real committed shadow
// tree carrying the parent's answer, reaching `TextInputController::reconcile` and`publish`ing the revert.
//
// The event handler here plays the parent: on every `topChange` it strips anything that is not a digit from the
// text and, if that changed anything, commits the stripped value back with the event count the edit carried.
// One accepted digit and one rejected letter is the sequence below — the accepted digit becomes part of what the
// parent approved, so the rejected letter's answer ("12" + "3", not the field's original mount value) can only
// be explained by the parent's commit actually reaching the buffer, not by coincidence.
//
// hello_react --type packages/core/test-bundles/text-input-reject.js /tmp/reject.png "3x"

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const mountedText = '12';
const instance = { name: 'field', stateNode: { node: null } };

const fieldProps = (text, mostRecentEventCount) => ({
  position: 'absolute',
  left: 40,
  top: 40,
  width: 300,
  height: 44,
  padding: 10,
  backgroundColor: 0xff1e2430 | 0,
  color: 0xfff2f4f8 | 0,
  fontSize: 18,
  accessible: true,
  text: text,
  mostRecentEventCount: mostRecentEventCount,
});

let node = fabric.createNode(10, 'TextInput', surfaceId, fieldProps(mountedText, 0), instance);

instance.stateNode.node = node;

const commit = (root) => {
  const children = fabric.createChildSet();

  fabric.appendChildToSet(children, root);
  fabric.completeRoot(surfaceId, children);
};

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;
  const text = payload && payload.text !== undefined ? ' text="' + payload.text + '"' : '';
  const eventCount = payload && payload.eventCount !== undefined ? ' eventCount=' + payload.eventCount : '';
  const contentSize =
    payload && payload.contentSize !== undefined
      ? ' contentSize=' + payload.contentSize.width + 'x' + payload.contentSize.height
      : '';

  console.log('text-input-reject: ' + type + ' on ' + name + text + eventCount + contentSize);

  if (type !== 'topChange') {
    return;
  }

  const digitsOnly = payload.text.replace(/[^0-9]/g, '');

  if (digitsOnly !== payload.text) {
    node = fabric.cloneNodeWithNewProps(node, fieldProps(digitsOnly, payload.eventCount));
    instance.stateNode.node = node;
    commit(node);
  }
});

commit(node);

console.log('text-input-reject: committed surface ' + surfaceId);
