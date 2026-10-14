import path from "node:path";

interface StagedCopy {
  readonly from: string;
  readonly to: string;
}

interface CoreBinarySources {
  readonly binaryPath: string;
  readonly binDirectory: string;
  readonly fontDirectory: string;
  readonly lddOutput: string;
  readonly shippedLibraries: ReadonlySet<string>;
}

const NONE = 0;

/** `ldd`'s `name => /path (address)` lines, keyed by name. `not found` and path-less lines are absent. */
const parseResolvedLibraries = (lddOutput: string): ReadonlyMap<string, string> =>
  new Map(
    lddOutput.split("\n").flatMap((line) => {
      const [name, arrow, resolved] = line.trim().split(" ");

      return arrow === "=>" && String(resolved).startsWith("/") ? [[String(name), String(resolved)] as const] : [];
    }),
  );

/**
 * What `pnpm pack:local` copies into `packages/core/bin`: the release `rnl_window`, every library the dependency
 * floor names as shipped payload rather than a host dependency (#357), taken from wherever `ldd` resolved it for
 * this build and found again through the binary's `$ORIGIN` runpath, and the vendored fonts. `portable.marker`,
 * written beside them, is what makes `ResourceResolver` read the fonts from there (#361). A shipped library `ldd`
 * did not resolve fails the plan by name.
 */
const planCoreBinary = ({
  binaryPath,
  binDirectory,
  fontDirectory,
  lddOutput,
  shippedLibraries,
}: CoreBinarySources): readonly StagedCopy[] => {
  const resolved = parseResolvedLibraries(lddOutput);
  const unresolved = [...shippedLibraries].filter((name) => !resolved.has(name));

  if (unresolved.length > NONE) {
    throw new Error(`rnl_window does not resolve the shipped libraries ${unresolved.join(", ")}`);
  }

  return [
    { from: binaryPath, to: path.join(binDirectory, "rnl_window") },
    ...[...shippedLibraries].map((name) => ({ from: String(resolved.get(name)), to: path.join(binDirectory, name) })),
    { from: fontDirectory, to: path.join(binDirectory, "fonts") },
  ];
};

export { planCoreBinary };
