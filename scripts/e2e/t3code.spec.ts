import {
  buildPairingLink,
  gradeWindowRun,
  overlayInstallArguments,
  overlayPackArguments,
  readPairingUrl,
  tailLines,
} from "./t3code.ts";
import { describe, expect, it } from "vitest";
import { packageAliases } from "@react-native-linux/cli/package-aliases.ts";

const TAIL_LINE_COUNT = 40;
const pairingUrl = "http://127.0.0.1:38731/pair#token=pairing-token";

describe("readPairingUrl", () => {
  it("reads the pairing URL the ready server prints", () => {
    expect(readPairingUrl(`T3 Code server is ready.\nPairing URL: ${pairingUrl}\nApp link: t3code://pair\n`)).toBe(
      pairingUrl,
    );
  });

  it("answers null before the server is ready", () => {
    expect(readPairingUrl("[15:16:37.466] INFO (#507): Running database migrations\n")).toBeNull();
  });
});

describe("buildPairingLink", () => {
  it("opens the pairing form on the pairing URL and connects at once", () => {
    const link = new URL(buildPairingLink(pairingUrl));

    expect(`${link.protocol}//${link.host}${link.pathname}`).toBe("t3code://connections/new");
    expect(link.searchParams.get("pairingUrl")).toBe(pairingUrl);
    expect(link.searchParams.get("autoConnect")).toBe("1");
  });
});

const presented = "[rnl-present] first frame presented\n";
const cleanRun = { exitCode: 0, hasSocket: true, isTraceClosed: true, signal: null, trace: presented };

describe("gradeWindowRun", () => {
  it("passes a run whose window presented a frame and exited cleanly with no fatal error", () => {
    expect(gradeWindowRun(cleanRun)).toStrictEqual([]);
  });

  it("leads with the first fatal JavaScript error", () => {
    const trace = `${presented}[js-error] fatal A\nwarn\n[js-error] fatal B\n`;

    expect(gradeWindowRun({ ...cleanRun, trace })).toStrictEqual(["startup stopped at\n[js-error] fatal A"]);
  });

  it("fails a run that never got a socket, a frame, a close or a clean exit", () => {
    expect(
      gradeWindowRun({ exitCode: null, hasSocket: false, isTraceClosed: false, signal: "SIGKILL", trace: "" }),
    ).toStrictEqual([
      "the compositor never created a wayland socket",
      'the window never printed "[rnl-present] first frame presented"',
      "the window did not exit within the run timeout",
      "the compositor ended with exit code null, signal SIGKILL",
    ]);
  });
});

describe("tailLines", () => {
  it("keeps the last forty lines", () => {
    const kept = Array.from({ length: TAIL_LINE_COUNT }, () => "kept").join("\n");

    expect(tailLines(`dropped\n${kept}\n`)).toBe(kept);
  });
});

describe("overlayPackArguments", () => {
  it("packs every overlay package the alias table names, and nothing else", () => {
    expect(overlayPackArguments(packageAliases, "/build/t3code/overlays")).toStrictEqual([
      "--filter",
      "@react-native-linux/enriched-markdown",
      "--filter",
      "@react-native-linux/gesture-handler",
      "--filter",
      "@react-native-linux/reanimated",
      "pack",
      "--pack-destination",
      "/build/t3code/overlays",
    ]);
  });
});

describe("overlayInstallArguments", () => {
  it("adds the tarballs to the mobile app without install scripts or the unpublished core peer", () => {
    expect(overlayInstallArguments(["/overlays/a.tgz", "/overlays/b.tgz"])).toStrictEqual([
      "--filter",
      "@t3tools/mobile",
      "add",
      "/overlays/a.tgz",
      "/overlays/b.tgz",
      "--ignore-scripts",
      "--config.auto-install-peers=false",
      "--config.strict-peer-dependencies=false",
    ]);
  });
});
