import { describe, expect, it } from "vitest";
import { planCoreBinary } from "./core-binary-plan.ts";

const LDD_OUTPUT = `	linux-vdso.so.1 (0x00007ffd)
	libhermesvm.so => /build/release/_deps/hermes-build/lib/libhermesvm.so (0x00007f5b)
	libfmt.so.12 => /usr/lib/libfmt.so.12 (0x00007f5c)
	/lib64/ld-linux-x86-64.so.2 (0x00007f5d)`;

describe("planCoreBinary", () => {
  it("stages the binary, each shipped library from where ldd resolved it, and the fonts, flat in bin", () => {
    expect(
      planCoreBinary({
        binDirectory: "/core/bin",
        binaryPath: "/build/release/bin/rnl_window",
        fontDirectory: "/core/fonts",
        lddOutput: LDD_OUTPUT,
        shippedLibraries: new Set(["libhermesvm.so", "libfmt.so.12"]),
      }),
    ).toStrictEqual([
      { from: "/build/release/bin/rnl_window", to: "/core/bin/rnl_window" },
      { from: "/build/release/_deps/hermes-build/lib/libhermesvm.so", to: "/core/bin/libhermesvm.so" },
      { from: "/usr/lib/libfmt.so.12", to: "/core/bin/libfmt.so.12" },
      { from: "/core/fonts", to: "/core/bin/fonts" },
    ]);
  });

  it("fails naming every shipped library ldd did not resolve", () => {
    expect(() =>
      planCoreBinary({
        binDirectory: "/b",
        binaryPath: "/rnl_window",
        fontDirectory: "/f",
        lddOutput: `${LDD_OUTPUT}\n\tlibx.so => not found`,
        shippedLibraries: new Set(["libjsi.so", "libfmt.so.12", "libx.so"]),
      }),
    ).toThrow("rnl_window does not resolve the shipped libraries libjsi.so, libx.so");
  });
});
