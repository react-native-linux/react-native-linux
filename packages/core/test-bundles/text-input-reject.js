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
// The fixture does not just log the trace, it asserts it: `assertNextEvent` checks every event against
// `expectedEvents` in order — type, and for `topChange`/`topSelectionChange`, the text and event count too — and
// throws on the first mismatch, which fails the run through the fatal-error gate `renderTypedGolden` already
// checks (`JsErrorReporter`/`hasReportedFatalError`). That is what makes an unexpected extra
// `topContentSizeChange`, or a `topChange` in the wrong place, a run failure rather than a line only a human
// reading the trace would notice. A run that stopped short of the full sequence — the revert never arriving, say
// — is not caught here: nothing throws when an event simply never comes. The golden image is what catches that
// half, because a field stuck on the rejected text paints differently from one that reverted.
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

// One accepted digit ("3"), one rejected letter ("x") answered by the parent's own commit of "123", named
// exactly: type, and — where the payload carries them — the text and the event count. `topContentSizeChange`
// and `topKeyDown`/`topKeyUp`/`topKeyPress` carry no text to check, so only their position and type are asserted.
const expectedEvents = [
  { type: 'topFocus' },
  { type: 'topKeyUp' },
  { type: 'topContentSizeChange' },
  { type: 'topSelectionChange', text: mountedText, eventCount: 0 },
  { type: 'topKeyDown' },
  { type: 'topKeyPress', eventCount: 0 },
  { type: 'topChange', text: '123', eventCount: 1 },
  { type: 'topContentSizeChange' },
  { type: 'topSelectionChange', text: '123', eventCount: 1 },
  { type: 'topKeyUp' },
  { type: 'topKeyDown' },
  { type: 'topKeyPress', eventCount: 1 },
  { type: 'topChange', text: '123x', eventCount: 2 },
  { type: 'topContentSizeChange' },
  { type: 'topSelectionChange', text: '123x', eventCount: 2 },
  { type: 'topKeyUp' },
  { type: 'topChange', text: '123', eventCount: 2 },
  { type: 'topContentSizeChange' },
  { type: 'topSelectionChange', text: '123', eventCount: 2 },
];

let eventIndex = 0;

const describe = (type, payload) => {
  const text = payload && payload.text !== undefined ? ' text="' + payload.text + '"' : '';
  const eventCount = payload && payload.eventCount !== undefined ? ' eventCount=' + payload.eventCount : '';
  const contentSize =
    payload && payload.contentSize !== undefined
      ? ' contentSize=' + payload.contentSize.width + 'x' + payload.contentSize.height
      : '';

  return type + text + eventCount + contentSize;
};

const assertNextEvent = (type, payload) => {
  const expected = expectedEvents[eventIndex];
  const position = eventIndex;

  eventIndex += 1;

  if (expected === undefined) {
    throw new Error(
      'text-input-reject: unexpected event ' +
        describe(type, payload) +
        ' after the expected sequence of ' +
        expectedEvents.length +
        ' events already ended',
    );
  }

  if (expected.type !== type) {
    throw new Error(
      'text-input-reject: expected ' + expected.type + ' at position ' + position + ' but got ' + describe(type, payload),
    );
  }

  if (expected.text !== undefined && (payload === null || payload === undefined || payload.text !== expected.text)) {
    throw new Error(
      'text-input-reject: expected ' +
        type +
        ' with text="' +
        expected.text +
        '" at position ' +
        position +
        ' but got ' +
        describe(type, payload),
    );
  }

  if (
    expected.eventCount !== undefined &&
    (payload === null || payload === undefined || payload.eventCount !== expected.eventCount)
  ) {
    throw new Error(
      'text-input-reject: expected ' +
        type +
        ' with eventCount=' +
        expected.eventCount +
        ' at position ' +
        position +
        ' but got ' +
        describe(type, payload),
    );
  }
};

fabric.registerEventHandler((instanceHandle, type, payload) => {
  const name = instanceHandle === null || instanceHandle === undefined ? 'unknown' : instanceHandle.name;

  console.log('text-input-reject: ' + describe(type, payload) + ' on ' + name);
  assertNextEvent(type, payload);

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
