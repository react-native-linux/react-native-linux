// The symbolication probe for issue #82: compiled to -O bytecode with a source map, it throws two frames deep, and
// goldens/release-golden.spec.ts maps the bytecode stack back to these line numbers.
function outer() {
  inner();
}
function inner() {
  throw new Error('release symbolication probe');
}
outer();
