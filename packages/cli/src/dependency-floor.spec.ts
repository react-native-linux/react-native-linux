import {
  DependencyFloorError,
  archDependencyList,
  computeDependencyFloor,
  debianDependencyLine,
  maximumVersion,
  olderThan,
  parseNeededLibraries,
  parseSymbolVersions,
  symbolFloor,
} from "./dependency-floor.ts";
import { describe, expect, it } from "vitest";

const resolvedDependencyCount = 2;

const versionNeedsSection = [
  "Version needs section '.gnu.version_r' contains 2 entries:",
  " 0x0000: Version: 1  File: libc.so.6  Cnt: 3",
  "  0x0010:   Name: GLIBC_2.28  Flags: none  Version: 3",
  "  0x0020:   Name: GLIBC_2.35  Flags: none  Version: 4",
  "  0x0030:   Name: GLIBC_2.2.5  Flags: none  Version: 2",
  " 0x0040: Version: 1  File: libstdc++.so.6  Cnt: 1",
  "  0x0050:   Name: GLIBCXX_3.4.29  Flags: none  Version: 5",
].join("\n");

const neededSection = [
  " 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]",
  " 0x0000000000000001 (NEEDED)             Shared library: [libm.so.6]",
  " 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]",
].join("\n");

const fullOutput = `${versionNeedsSection}\n${neededSection}\n`;

describe("parseSymbolVersions", () => {
  it("reads every required symbol version out of the version needs section", () => {
    expect(parseSymbolVersions(fullOutput)).toEqual(["GLIBC_2.28", "GLIBC_2.35", "GLIBC_2.2.5", "GLIBCXX_3.4.29"]);
  });

  it("reads nothing from a binary with no versioned symbols", () => {
    expect(parseSymbolVersions("there is no version needs section here\n")).toEqual([]);
  });

  it("stops at tool output that merely mentions the section", () => {
    expect(parseSymbolVersions("Version needs section appears later\nName: GLIBC_9.9\n")).toEqual(["GLIBC_9.9"]);
  });
});

describe("parseNeededLibraries", () => {
  it("reads every DT_NEEDED entry in loader order", () => {
    expect(parseNeededLibraries(neededSection)).toEqual(["libc.so.6", "libm.so.6", "libstdc++.so.6"]);
  });

  it("reads nothing when the binary is statically linked", () => {
    expect(parseNeededLibraries("(NEEDED) nothing here\n")).toEqual([]);
  });
});

describe("olderThan", () => {
  it("compares numerically, not lexicographically", () => {
    expect(olderThan("2.7", "2.10")).toBe(true);
    expect(olderThan("2.10", "2.7")).toBe(false);
  });

  it("compares segment by segment and pads the shorter version with zeros", () => {
    expect(olderThan("2.35", "2.35.1")).toBe(true);
    expect(olderThan("2.35.1", "2.35")).toBe(false);
    expect(olderThan("2.35", "2.35")).toBe(false);
  });
});

describe("maximumVersion", () => {
  it("picks the numeric maximum of the family", () => {
    expect(maximumVersion(["2.28", "2.35", "2.2.5"])).toBe("2.35");
  });

  it("is null when the family is absent", () => {
    expect(maximumVersion([])).toBeNull();
  });
});

describe("symbolFloor", () => {
  it("keeps the three ABI families apart", () => {
    const floor = symbolFloor(["GLIBC_2.28", "GLIBCXX_3.4.29", "CXXABI_1.3.9", "V_0.5.0", "GCC_3.0"]);

    expect(floor).toEqual({ cxxabi: "1.3.9", glibc: "2.28", glibcxx: "3.4.29" });
  });

  it("leaves a family null when the binary needs none of it", () => {
    expect(symbolFloor(["GLIBC_2.28"])).toEqual({ cxxabi: null, glibc: "2.28", glibcxx: null });
  });
});

describe("computeDependencyFloor", () => {
  it("resolves every DT_NEEDED entry to its package, deduping the packages libraries share", () => {
    const floor = computeDependencyFloor(fullOutput);

    expect(floor.dependencies).toEqual([
      { arch: "glibc", debian: "libc6", library: "libc.so.6" },
      { arch: "gcc-libs", debian: "libstdc++6", library: "libstdc++.so.6" },
    ]);
  });

  it("skips the libraries the package itself ships", () => {
    const output = `${neededSection}\n (NEEDED)             Shared library: [libjsi.so]\n`;

    expect(computeDependencyFloor(output).dependencies).toHaveLength(resolvedDependencyCount);
  });

  it("skips the family-constrained libraries, whose package the table already names", () => {
    const output = `${neededSection}\n (NEEDED)             Shared library: [libatomic.so.1]\n`;

    expect(computeDependencyFloor(output).dependencies).toHaveLength(resolvedDependencyCount);
  });

  it("fails by name on a DT_NEEDED entry nothing accounts for", () => {
    const output = `${neededSection}\n (NEEDED)             Shared library: [libwe-do-not-ship.so.7]\n`;

    expect(() => computeDependencyFloor(output)).toThrow(DependencyFloorError);
    expect(() => computeDependencyFloor(output)).toThrow(/needs "libwe-do-not-ship\.so\.7"/u);
  });
});

describe("debianDependencyLine", () => {
  it("spells the glibc and libstdc++ floors as version constraints and the rest as names", () => {
    const line = debianDependencyLine(computeDependencyFloor(fullOutput));

    expect(line).toBe("libc6 (>= 2.35), libstdc++6 (>= 11)");
  });

  it("constrains nothing when the binary needs no versioned symbols", () => {
    const floor = computeDependencyFloor(neededSection);

    expect(debianDependencyLine(floor)).toBe("libc6, libstdc++6");
  });

  it("fails by name when the GLIBCXX floor has no release epoch in the table", () => {
    const output = fullOutput.replace("GLIBCXX_3.4.29", "GLIBCXX_3.4.99");

    expect(() => debianDependencyLine(computeDependencyFloor(output))).toThrow(DependencyFloorError);
    expect(() => debianDependencyLine(computeDependencyFloor(output))).toThrow(/"3\.4\.99"/u);
  });
});

describe("archDependencyList", () => {
  it("carries the glibc floor and bare package names for the rest", () => {
    const list = archDependencyList(computeDependencyFloor(fullOutput));

    expect(list).toEqual(["glibc>=2.35", "gcc-libs"]);
  });

  it("is all bare names when the binary needs no versioned symbols", () => {
    expect(archDependencyList(computeDependencyFloor(neededSection))).toEqual(["glibc", "gcc-libs"]);
  });
});
