// #372's other two generics: `serif` and `monospace` are resolved from fontconfig's own answer, which is the
// point of asking for a generic rather than a failure to find one (see #70's exemption). That answer is whatever
// the host has installed, so unlike font-generics.js this bundle has no checked-in golden: golden.spec.ts renders
// it proof-only, asserting only that the render succeeds, not what it looks like. See docs/cpp-toolchain.md,
// *The default fontFamily (#372)*.

const surfaceId = 1;
const fabric = globalThis.nativeFabricUIManager;

if (fabric === undefined) {
  throw new Error('nativeFabricUIManager was not installed');
}

const retainedInstances = [];
const buildTag = (function () {
  let counter = 1;

  return function () {
    counter += 1;

    return counter;
  };
})();

const make = (componentName) => (props) => (children) => {
  const handle = {};
  const created = fabric.createNode(buildTag(), componentName, surfaceId, props, handle);

  retainedInstances.push(handle);

  for (const child of children) {
    fabric.appendChild(created, child);
  }

  return created;
};

const makeText = (value) => make('RawText')({ text: value })([]);
const makeParagraph = (props) => (children) => make('Paragraph')(props)(children);
const makeView = (props) => (children) => make('View')(props)(children);

const foreground = 0xfff2f4f8 | 0;
const secondary = 0xff9aa4b2 | 0;

const heading = makeParagraph({
  position: 'absolute',
  left: 40,
  top: 24,
  width: 720,
  color: foreground,
  fontSize: 16,
  fontWeight: 'bold',
})([makeText('Default fontFamily (#372): fontconfig generics, proof-only, no checked-in golden')]);

const genericFamilies = ['serif', 'monospace'];
const fontSizes = [16, 40];
const wordsBySize = { 16: 'The quick brown fox jumps over the lazy dog', 40: 'Aa Bb Cc' };

const rows = [];
let rowTop = 64;

for (const fontFamily of genericFamilies) {
  for (const fontSize of fontSizes) {
    rows.push(
      makeParagraph({ position: 'absolute', left: 40, top: rowTop, width: 720, color: secondary, fontSize: 12 })([
        makeText('fontFamily "' + fontFamily + '", fontSize ' + fontSize),
      ]),
    );
    rows.push(
      makeParagraph({
        position: 'absolute',
        left: 40,
        top: rowTop + 18,
        width: 720,
        color: foreground,
        fontFamily,
        fontSize,
      })([makeText(wordsBySize[fontSize])]),
    );
    rowTop += fontSize + (fontSize < 20 ? 28 : 36);
  }
}

const root = makeView({ flex: 1 })([heading, ...rows]);

const rootChildren = fabric.createChildSet();

fabric.appendChildToSet(rootChildren, root);
fabric.completeRoot(surfaceId, rootChildren);

console.log('font-generics-fontconfig: committed surface ' + surfaceId);
