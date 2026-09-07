// #372: what an unset `fontFamily`, `sans-serif` and `system-ui` resolve to — the vendored Noto Sans in
// packages/core/fonts, asked for directly rather than through fontconfig, so a host's own `sans-serif` alias
// answering the wrong face — electron/electron#53499's KDE Wayland interface rendered entirely in monospace —
// cannot reach this picture. See docs/cpp-toolchain.md, *The default fontFamily (#372)*.
//
// `serif` and `monospace` are deliberately not in this picture: fontconfig answers those from whatever the host
// has installed, so a golden containing them would not be reproducible across hosts. They are proved instead by
// font-generics-fontconfig.js, rendered proof-only with no checked-in golden — see golden.spec.ts.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

// Weakly-held instance handles: this keeps React's side of each node alive for the life of the bundle.
const instances = [];
let nextTag = 2;

function spawn(componentName, props, children) {
  const handle = {};
  const kids = children || [];
  const created = fabric.createNode(nextTag, componentName, surfaceId, props, handle);

  instances.push(handle);
  nextTag += 1;
  kids.forEach(function (kid) {
    fabric.appendChild(created, kid);
  });

  return created;
}

function line(value) {
  return spawn('RawText', { text: value });
}

function block(props, children) {
  return spawn('Paragraph', props, children);
}

function box(props, children) {
  return spawn('View', props, children);
}

const paperWhite = 0xfff2f4f8 | 0;
const dim = 0xff9aa4b2 | 0;

function caption(top, words) {
  return block({ position: 'absolute', left: 40, top, width: 720, color: dim, fontSize: 12 }, [line(words)]);
}

const requests = [
  { fontFamily: undefined, label: 'fontFamily unset' },
  { fontFamily: 'sans-serif', label: 'fontFamily "sans-serif"' },
  { fontFamily: 'system-ui', label: 'fontFamily "system-ui"' },
];
const sizes = [16, 40];
const sample = { 16: 'The quick brown fox jumps over the lazy dog', 40: 'Aa Bb Cc' };

const rows = [];
let cursor = 64;

requests.forEach(function (request) {
  sizes.forEach(function (fontSize) {
    const textProps = { position: 'absolute', left: 40, top: cursor + 18, width: 720, color: paperWhite, fontSize };

    if (request.fontFamily !== undefined) {
      textProps.fontFamily = request.fontFamily;
    }

    rows.push(caption(cursor, request.label + ', fontSize ' + fontSize));
    rows.push(block(textProps, [line(sample[fontSize])]));
    cursor += fontSize < 20 ? 44 : 76;
  });
});

const heading = block(
  { position: 'absolute', left: 40, top: 24, width: 720, color: paperWhite, fontSize: 16, fontWeight: 'bold' },
  [line('Default fontFamily (#372): resolved from the vendored Noto Sans, not the host')],
);

const root = box({ flex: 1 }, [heading].concat(rows));

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('font-generics: committed surface ' + surfaceId);
