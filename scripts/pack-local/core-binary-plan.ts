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

/** `libfmt.so.12` and `libfmt.so.10` are one library at two host versions; `libfmt.so` is what they share. */
const sonameStem = (name: string): string => name.replace(/\.so(?:\.\d+)*$/u, ".so");

/**
 * What `pnpm pack:local` copies into `packages/core/bin`: the release `rnl_window`, every library the dependency
 * floor names as shipped payload rather than a host dependency (#357), taken from wherever `ldd` resolved it for
 * this build and found again through the binary's `$ORIGIN` runpath, and the vendored fonts. `portable.marker`,
 * written beside them, is what makes `ResourceResolver` read the fonts from there (#361). A shipped library is
 * matched by its name up to `.so`, because the soname version is the host's: the floor names Arch's `libfmt.so.12`
 * and ubuntu-24.04 links `libfmt.so.10`. One that `ldd` did not resolve at all fails the plan by name.
 */
const planCoreBinary = ({
  binaryPath,
  binDirectory,
  fontDirectory,
  lddOutput,
  shippedLibraries,
}: CoreBinarySources): readonly StagedCopy[] => {
  const resolved = [...parseResolvedLibraries(lddOutput)].map(([name, resolvedPath]) => ({ name, resolvedPath }));
  const findShipped = (shipped: string): (typeof resolved)[number] | null =>
    resolved.find(({ name }) => sonameStem(name) === sonameStem(shipped)) ?? null;
  const unresolved = [...shippedLibraries].filter((shipped) => findShipped(shipped) === null);

  if (unresolved.length > NONE) {
    throw new Error(`rnl_window does not resolve the shipped libraries ${unresolved.join(", ")}`);
  }

  const found = resolved.filter(({ name }) =>
    [...shippedLibraries].some((shipped) => sonameStem(name) === sonameStem(shipped)),
  );

  return [
    { from: binaryPath, to: path.join(binDirectory, "rnl_window") },
    ...found.map(({ name, resolvedPath }) => ({ from: resolvedPath, to: path.join(binDirectory, name) })),
    { from: fontDirectory, to: path.join(binDirectory, "fonts") },
  ];
};

export { planCoreBinary };
