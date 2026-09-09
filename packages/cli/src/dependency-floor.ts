/**
 * The runtime dependency floor of #357: what the shipped binary actually demands of the machine it lands on,
 * read off the ELF itself rather than hand-maintained and left to drift — the omission behind tauri#12883 and
 * the 93 comments of tauri#9662.
 *
 * Two `readelf` sections carry everything. `.gnu.version_r` (`readelf -V`) names every versioned symbol the
 * binary requires — the `GLIBC_*`, `GLIBCXX_*` and `CXXABI_*` maxima are the floor, because a requirement is
 * set by the oldest library that shipped the newest symbol it asks for. `.dynamic` (`readelf -d`) names every
 * `DT_NEEDED` library, and each one must resolve to a host package on each target distribution or to the set
 * of libraries the package itself ships — an entry that resolves to neither fails by name, because the
 * alternative is the failure at `dlopen` time on a user's machine.
 *
 * Everything here is pure over the tool's text output, which is what puts it under the 100% gate: the fixture
 * is the same text `readelf` prints, so a toolchain format change is a fixture diff, not a silent miss.
 */

const versionNeedsMarker = "Version needs section";
const sectionStartFloor = 0;

interface SymbolFloor {
  readonly cxxabi: string | null;
  readonly glibc: string | null;
  readonly glibcxx: string | null;
}

/** One host library, and the package that provides it on each target distribution. */
interface LibraryPackage {
  readonly arch: string;
  readonly debian: string;
}

interface ResolvedDependency {
  readonly arch: string;
  readonly debian: string;
  /** A representative `DT_NEEDED` name the package provides; `libc6` is resolved from both libc and libm. */
  readonly library: string;
}

interface DependencyFloor {
  readonly dependencies: readonly ResolvedDependency[];
  readonly symbolFloor: SymbolFloor;
}

/** Both floors fail with this class; the message names the library or symbol version that could not be honoured. */
class DependencyFloorError extends Error {
  public constructor(message: string) {
    super(message);
    this.name = "DependencyFloorError";
  }
}

const symbolPrefixes = ["CXXABI", "GLIBC", "GLIBCXX"] as const;
const symbolSeparatorLength = 1;

type SymbolPrefix = (typeof symbolPrefixes)[number];

/** Every `Name:` inside the `.gnu.version_r` section — the versioned symbols the binary requires. */
const parseSymbolVersions = (readelfOutput: string): readonly string[] => {
  const sectionStart = Math.max(readelfOutput.indexOf(versionNeedsMarker), sectionStartFloor);

  const section = readelfOutput.slice(sectionStart);

  // v8 ignore next — a matched required named group always defines itself, so the fallbacks are unreachable.
  return [...section.matchAll(/\bName: (?<symbol>\S+)/gu)].map((match) => match.groups?.["symbol"] ?? "");
};

/** Every `DT_NEEDED` library in the `.dynamic` section, in the order the loader sees them. */
const parseNeededLibraries = (readelfOutput: string): readonly string[] => {
  const neededMarker = "(NEEDED)";
  const sharedLibraryPrefix = "Shared library: [";
  const sharedLibraryPrefixLength = sharedLibraryPrefix.length;
  const librarySuffix = "]";

  return readelfOutput
    .split("\n")
    .filter((line) => line.includes(neededMarker) && line.includes(sharedLibraryPrefix) && line.includes(librarySuffix))
    .map((line) => {
      const start = line.indexOf(sharedLibraryPrefix) + sharedLibraryPrefixLength;
      const end = line.indexOf(librarySuffix, start);

      return line.slice(start, end);
    });
};

const missingSegmentValue = 0;

const versionSegments = (version: string): readonly number[] => version.split(".").map(Number);

/** Numeric, segment by segment: `2.7` is older than `2.10`, which a lexicographic compare gets backwards. */
const olderThan = (left: string, right: string): boolean => {
  const leftSegments = versionSegments(left);
  const rightSegments = versionSegments(right);
  const segmentCount = Math.max(leftSegments.length, rightSegments.length);
  const segmentIndexes = Array.from({ length: segmentCount }, (__unused, segment) => segment);
  const firstDifference = segmentIndexes.find((index) => leftSegments[index] !== rightSegments[index]) ?? segmentCount;

  return (
    (leftSegments[firstDifference] ?? missingSegmentValue) < (rightSegments[firstDifference] ?? missingSegmentValue)
  );
};

const maximumVersion = (versions: readonly string[]): string | null => {
  let maximum: string | null = null;

  for (const version of versions) {
    if (maximum === null || olderThan(maximum, version)) {
      maximum = version;
    }
  }

  return maximum;
};

/**
 * The floor per symbol family. Only the three ABI families constrain a package version; the rest of what
 * `.gnu.version_r` names (`GCC_3.0`, `LIBATOMIC_1.0`, a library's own `V_*`) belongs to libraries the table
 * below resolves by name alone.
 */
const symbolFloor = (versions: readonly string[]): SymbolFloor => {
  const byPrefix: Record<SymbolPrefix, string[]> = { CXXABI: [], GLIBC: [], GLIBCXX: [] };

  for (const version of versions) {
    for (const prefix of symbolPrefixes) {
      if (version.startsWith(`${prefix}_`)) {
        byPrefix[prefix].push(version.slice(prefix.length + symbolSeparatorLength));
      }
    }
  }

  return {
    cxxabi: maximumVersion(byPrefix.CXXABI),
    glibc: maximumVersion(byPrefix.GLIBC),
    glibcxx: maximumVersion(byPrefix.GLIBCXX),
  };
};

/** The host-owned libraries, and the package providing each on the two target distributions. */
const hostLibraries: ReadonlyMap<string, LibraryPackage> = new Map([
  ["ld-linux-x86-64.so.2", { arch: "glibc", debian: "libc6" }],
  ["libc.so.6", { arch: "glibc", debian: "libc6" }],
  ["libfontconfig.so.1", { arch: "fontconfig", debian: "libfontconfig1" }],
  ["libfreetype.so.6", { arch: "freetype2", debian: "libfreetype6" }],
  ["libgcc_s.so.1", { arch: "gcc-libs", debian: "libgcc-s1" }],
  ["libm.so.6", { arch: "glibc", debian: "libc6" }],
  ["libstdc++.so.6", { arch: "gcc-libs", debian: "libstdc++6" }],
  ["libsystemd.so.0", { arch: "systemd-libs", debian: "libsystemd0" }],
  ["libvulkan.so.1", { arch: "vulkan-icd-loader", debian: "libvulkan1" }],
  ["libwayland-client.so.0", { arch: "wayland", debian: "libwayland-client0" }],
  ["libxkbcommon.so.0", { arch: "libxkbcommon", debian: "libxkbcommon0" }],
]);

/**
 * Libraries the package itself ships — the vendored Hermes, JSI, double-conversion and fmt. They are payload,
 * not dependencies, and the portable-bundle issue owns that list.
 */
const shippedLibraries: ReadonlySet<string> = new Set([
  "libdouble-conversion.so.3",
  "libfmt.so.12",
  "libhermesvm.so",
  "libjsi.so",
]);

/** Libraries whose floor need not constrain a version, because their packages ship the whole family. */
const unconstrainedLibraries: ReadonlySet<string> = new Set(["libatomic.so.1"]);

/**
 * The libstdc++ release epoch that first shipped each `GLIBCXX_*` version — the anchors of the libstdc++ ABI
 * timeline. A floor the table does not name fails rather than guessing, and the toolchain's release notes name
 * the row to add. `CXXABI_*` is deliberately not mapped the same way: the two symbol families ship in the same
 * libstdc++6, the GLIBCXX epoch is the finer-grained of the two, and the checked-in lock records both floors,
 * so a CXXABI-only bump still surfaces as a reviewed diff.
 */
const glibcxxReleaseEpochs: ReadonlyMap<string, string> = new Map([
  ["3.4", "3.4.1"],
  ["3.4.19", "4.8"],
  ["3.4.21", "5"],
  ["3.4.24", "8"],
  ["3.4.26", "9"],
  ["3.4.28", "10"],
  ["3.4.29", "11"],
  ["3.4.30", "12"],
  ["3.4.31", "13.1"],
  ["3.4.32", "13.2"],
  ["3.4.33", "14.1"],
]);

const libstdcxxEpoch = (floor: SymbolFloor): string | null => {
  if (floor.glibcxx === null) {
    return null;
  }

  const epoch = glibcxxReleaseEpochs.get(floor.glibcxx) ?? null;

  if (epoch === null) {
    throw new DependencyFloorError(
      `the binary needs symbol version "${floor.glibcxx}", which has no libstdc++ release epoch in the ` +
        "mapping table; add the row the toolchain's release notes name",
    );
  }

  return epoch;
};

/** The dependency a `DT_NEEDED` entry resolves to, or null when the package itself ships the library. */
const resolveLibrary = (library: string): ResolvedDependency | null => {
  const resolved = hostLibraries.get(library) ?? null;

  if (resolved === null && !shippedLibraries.has(library) && !unconstrainedLibraries.has(library)) {
    throw new DependencyFloorError(
      `the binary needs "${library}", which is neither a known host package nor shipped by the package itself; ` +
        "add it to the library table or stop linking it",
    );
  }

  return resolved === null ? null : { arch: resolved.arch, debian: resolved.debian, library };
};

/**
 * The whole floor from one `readelf -V -d` dump. Every `DT_NEEDED` entry must resolve to a host package (a
 * package deduped across the libraries that provide it — libc and libm are one `libc6`) or to the shipped set;
 * the symbol families become the version constraints the formats spell differently.
 */
const computeDependencyFloor = (readelfOutput: string): DependencyFloor => {
  const dependencies: ResolvedDependency[] = [];
  const seenPackages = new Set<string>();

  for (const resolved of parseNeededLibraries(readelfOutput).map((library) => resolveLibrary(library))) {
    if (resolved !== null) {
      const packageKey = `${resolved.debian}|${resolved.arch}`;

      if (!seenPackages.has(packageKey)) {
        seenPackages.add(packageKey);
        dependencies.push(resolved);
      }
    }
  }

  return { dependencies, symbolFloor: symbolFloor(parseSymbolVersions(readelfOutput)) };
};

/** The `Depends:` value for a `.deb` control file: names comma-joined, the two constrained floors spelled out. */
const debianDependencyLine = (floor: DependencyFloor): string => {
  const epoch = libstdcxxEpoch(floor.symbolFloor);

  return floor.dependencies
    .map(({ debian }) => {
      if (debian === "libc6" && floor.symbolFloor.glibc !== null) {
        return `libc6 (>= ${floor.symbolFloor.glibc})`;
      }

      if (debian === "libstdc++6" && epoch !== null) {
        return `libstdc++6 (>= ${epoch})`;
      }

      return debian;
    })
    .join(", ");
};

/** The `depends=(…)` entries for a PKGBUILD: names only, except the glibc floor a rolling release still honours. */
const archDependencyList = (floor: DependencyFloor): readonly string[] =>
  floor.dependencies
    .map(({ arch }) =>
      arch === "glibc" && floor.symbolFloor.glibc !== null ? `glibc>=${floor.symbolFloor.glibc}` : arch,
    )
    .filter((entry, index, entries) => entries.indexOf(entry) === index);

export {
  DependencyFloorError,
  archDependencyList,
  computeDependencyFloor,
  debianDependencyLine,
  maximumVersion,
  olderThan,
  parseNeededLibraries,
  parseSymbolVersions,
  symbolFloor,
};
