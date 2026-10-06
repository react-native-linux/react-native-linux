// Issue #433's e2e half: laying out at width A, then B, then A gives back the layout from A. The fixture is
// core#58294's shape — a wrapped Text with `flex: 1` in a row — plus a content-sized card whose height is the
// wrapped text's, both reporting through onLayout. The scenario drags the window 800 → 500 → 800; the first time
// the surface is 800 wide the two layouts are recorded, and every later time it is 800 wide they must equal them.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Named on the handle, which is what a topLayout comes back with; retained, because C++ holds handles weakly.
const handles = [];

const make = (name, componentName, props) => {
  const handle = { name, stateNode: { node: null } };

  handles.push(handle);
  handle.stateNode.node = fabric.createNode(2 * handles.length, componentName, surfaceId, props, handle);

  return handle.stateNode.node;
};

const prose =
  'A wrapped paragraph long enough to take several lines at five hundred pixels and fewer at eight hundred, so ' +
  'the drag moves its wrap points and a stale cached measurement would show.';
const text = (name, style) => {
  const paragraph = make(name, 'Paragraph', { color: 0xfff2f4f8 | 0, fontSize: 14, onLayout: true, ...style });

  fabric.appendChild(paragraph, make(name + '-text', 'RawText', { text: prose }));

  return paragraph;
};

const surface = make('surface', 'View', { flex: 1, backgroundColor: 0xff11141a | 0, onLayout: true });
const row = make('row', 'View', { flexDirection: 'row', width: '70%', onLayout: true });
const card = make('card', 'View', { alignSelf: 'flex-start', maxWidth: '50%', padding: 8, onLayout: true });

fabric.appendChild(row, make('marker', 'View', { width: 24, height: 24, backgroundColor: 0xff61afef | 0 }));
fabric.appendChild(row, text('row-text', { flex: 1 }));
fabric.appendChild(card, text('card-text', {}));
fabric.appendChild(surface, row);
fabric.appendChild(surface, card);

const latest = new Map();
let recordedAtOriginalWidth = null;
let relaidOut = false;
const originalWidth = 800;

const describe = () =>
  ['row', 'row-text', 'card', 'card-text']
    .map((name) => {
      const layout = latest.get(name);

      return name + ' ' + [layout.x, layout.y, layout.width, layout.height].map((value) => value.toFixed(2)).join(',');
    })
    .join('; ');

let comparisonPending = false;
let restoredReported = false;

// Every node's topLayout for one commit arrives together, so the comparison waits for the batch to finish. Any
// tracked node's topLayout schedules one, so a late child layout at 800 is compared too, not only a width change.
const compareOnceSettled = () => {
  if (comparisonPending) {
    return;
  }

  comparisonPending = true;
  setTimeout(() => {
    comparisonPending = false;

    if (latest.size < 5) {
      return;
    }

    const now = describe();

    // Proof the drag moved something: without it, "restored" could pass on a fixture nothing ever relaid out.
    if (latest.get('surface').width !== originalWidth) {
      if (recordedAtOriginalWidth !== null && now !== recordedAtOriginalWidth && !relaidOut) {
        relaidOut = true;
        console.log('resize-round-trip: layout changed during the drag');
      }

      return;
    }

    if (recordedAtOriginalWidth === null) {
      recordedAtOriginalWidth = now;
      console.log('resize-round-trip: layout at 800 recorded');
    } else if (now !== recordedAtOriginalWidth) {
      console.log('resize-round-trip: layout differs: ' + now + ' | was ' + recordedAtOriginalWidth);
    } else if (relaidOut && !restoredReported) {
      restoredReported = true;
      console.log('resize-round-trip: layout restored');
    }
  }, 0);
};

fabric.registerEventHandler((instanceHandle, type, payload) => {
  if (type !== 'topLayout') {
    return;
  }

  latest.set(instanceHandle.name, payload.layout);
  compareOnceSettled();
});

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, surface);
fabric.completeRoot(surfaceId, rootChildren);

console.log('resize-round-trip: committed surface ' + surfaceId);
