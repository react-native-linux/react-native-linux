import path from "node:path";

interface StagedCopy {
  readonly from: string;
  readonly to: string;
}

/**
 * What `pnpm pack:local` copies into `packages/core/bin`: the release `rnl_window`, the two shared objects it
 * finds through its `$ORIGIN` runpath, and the vendored fonts. `portable.marker`, written beside them, is what
 * makes `ResourceResolver` read the fonts from there (#361); everything else it links is a host library.
 */
const planCoreBinary = (buildDirectory: string, fontDirectory: string, binDirectory: string): readonly StagedCopy[] => [
  { from: path.join(buildDirectory, "bin", "rnl_window"), to: path.join(binDirectory, "rnl_window") },
  {
    from: path.join(buildDirectory, "_deps", "hermes-build", "lib", "libhermesvm.so"),
    to: path.join(binDirectory, "libhermesvm.so"),
  },
  {
    from: path.join(buildDirectory, "_deps", "hermes-build", "jsi", "libjsi.so"),
    to: path.join(binDirectory, "libjsi.so"),
  },
  { from: fontDirectory, to: path.join(binDirectory, "fonts") },
];

export { planCoreBinary };
