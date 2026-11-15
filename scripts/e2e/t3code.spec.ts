import { buildPairingLink, findFatalErrors, readPairingUrl } from "./t3code.ts";
import { describe, expect, it } from "vitest";

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

describe("findFatalErrors", () => {
  it("keeps only the fatal JavaScript errors, in trace order", () => {
    const trace = "[rnl-present] first frame presented\n[js-error] fatal A\nwarn\n[js-error] fatal B\n";

    expect(findFatalErrors(trace)).toStrictEqual(["[js-error] fatal A", "[js-error] fatal B"]);
  });
});
