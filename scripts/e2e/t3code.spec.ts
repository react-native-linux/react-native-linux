import {
  CHECKOUT_INSTALL_ARGUMENTS,
  OVERLAY_DIRECTORY_NAME,
  T3CODE_REVISION,
  buildPairingLink,
  describeForeignCheckout,
  gradeWindowRun,
  installVariants,
  overlayInstallArguments,
  overlayPackArguments,
  readPairingUrl,
  tailLines,
} from "./t3code.ts";
import { afterEach, beforeEach, describe, expect, it } from "vitest";
import {
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  realpathSync,
  rmSync,
  symlinkSync,
  writeFileSync,
} from "node:fs";
import { packageAliases } from "@react-native-linux/cli/package-aliases.ts";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";

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
const cleanRun = {
  exitCode: 0,
  firstCommitMs: 97_000,
  hasSocket: true,
  isTraceClosed: true,
  signal: null,
  trace: presented,
};
const statusBarError =
  "[console.error] Invariant Violation: TurboModuleRegistry.getEnforcing(...): 'StatusBarManager' could not be found.";
const componentError = "[console.error] TypeError: Cannot read property 'default' of undefined";

describe("gradeWindowRun", () => {
  it("passes a run whose window presented, committed a view and exited cleanly with no startup error", () => {
    expect(gradeWindowRun(cleanRun)).toStrictEqual([]);
  });

  it("fails an empty window: a run that presented and exited cleanly but never committed a view", () => {
    expect(gradeWindowRun({ ...cleanRun, firstCommitMs: null })).toStrictEqual([
      "T3 Code never committed a view under the surface root",
    ]);
  });

  it("fails a root render error React reported only through console.error, and leads with it", () => {
    const trace = `${presented}Running "main"\n${statusBarError}\n${componentError}\n  isComponentError: true }\n`;

    expect(gradeWindowRun({ ...cleanRun, firstCommitMs: null, trace })).toStrictEqual([
      `startup reported\n${statusBarError}\n${componentError}`,
      "T3 Code never committed a view under the surface root",
    ]);
  });

  it("fails a run that never got a socket, a frame, a close or a clean exit", () => {
    expect(
      gradeWindowRun({
        exitCode: null,
        firstCommitMs: null,
        hasSocket: false,
        isTraceClosed: false,
        signal: "SIGKILL",
        trace: "",
      }),
    ).toStrictEqual([
      "the compositor never created a wayland socket",
      'the window never printed "[rnl-present] first frame presented"',
      "T3 Code never committed a view under the surface root",
      "the window did not exit within the run timeout",
      "the compositor ended with exit code null, signal SIGKILL",
    ]);
  });
});

const variantsFixtureFiles = ["Progress.linux.tsx", "controlValues.linux.ts", "AppText.tsx", "Progress.linux.tsx.map"];

const writeFile = (filePath: string, text: string): void => {
  mkdirSync(path.dirname(filePath), { recursive: true });
  writeFileSync(filePath, text);
};

const commitAll = (root: string): void => {
  const identity = ["-c", "user.name=fixture", "-c", "user.email=fixture@localhost"];

  spawnSync("git", ["-C", root, "add", "--all"]);
  spawnSync("git", ["-C", root, ...identity, "commit", "--quiet", "--message", "fixture"]);
};

describe("installVariants", () => {
  const temporaryDirectory = realpathSync(tmpdir());
  let workspace = "";
  let variantsDirectory = "";
  let mobileRoot = "";
  let uiDirectory = "";

  beforeEach(() => {
    workspace = mkdtempSync(path.join(temporaryDirectory, "rnl-t3code-variants-"));
    variantsDirectory = path.join(workspace, "variants");
    mobileRoot = path.join(workspace, "mobile");
    uiDirectory = path.join(mobileRoot, "src", "components", "ui");
    for (const name of variantsFixtureFiles) {
      writeFile(path.join(variantsDirectory, "components", "ui", name), name);
    }
    writeFile(path.join(uiDirectory, "Progress.tsx"), "shared");
    spawnSync("git", ["-C", mobileRoot, "init", "--quiet"]);
    commitAll(mobileRoot);
  });

  afterEach(() => {
    rmSync(workspace, { force: true, recursive: true });
  });

  it("copies each .linux.ts and .linux.tsx beside the file it replaces, again over its own copies", () => {
    expect([
      installVariants(variantsDirectory, mobileRoot),
      installVariants(variantsDirectory, mobileRoot),
    ]).toStrictEqual([null, null]);
    const copied = variantsFixtureFiles.map((name) => existsSync(path.join(uiDirectory, name)));

    expect(copied).toStrictEqual([true, true, false, false]);
  });

  it("refuses to overwrite a variant T3 Code tracks, and copies nothing", () => {
    writeFile(path.join(uiDirectory, "Progress.linux.tsx"), "T3 Code's own");
    commitAll(mobileRoot);

    expect(installVariants(variantsDirectory, mobileRoot)).toContain("src/components/ui/Progress.linux.tsx");
    expect([
      readFileSync(path.join(uiDirectory, "Progress.linux.tsx"), "utf8"),
      existsSync(path.join(uiDirectory, "controlValues.linux.ts")),
    ]).toStrictEqual(["T3 Code's own", false]);
  });

  it("refuses when git cannot list the checkout's files", () => {
    expect(installVariants(variantsDirectory, path.join(workspace, "missing"))).toContain("git could not tell");
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

describe("CHECKOUT_INSTALL_ARGUMENTS", () => {
  it("installs the workspace packages the mobile app and the server depend on, not the mobile app alone", () => {
    expect(CHECKOUT_INSTALL_ARGUMENTS).toStrictEqual([
      "install",
      "--frozen-lockfile",
      "--ignore-scripts",
      "--filter",
      "@t3tools/mobile...",
      "--filter",
      "t3...",
    ]);
  });
});

describe("overlayInstallArguments", () => {
  it("adds the tarballs to the mobile app without install scripts, keeping the checkout's lockfile settings", () => {
    expect(overlayInstallArguments(["/overlays/a.tgz", "/overlays/b.tgz"])).toStrictEqual([
      "--filter",
      "@t3tools/mobile",
      "add",
      "/overlays/a.tgz",
      "/overlays/b.tgz",
      "--ignore-scripts",
    ]);
  });
});

describe("describeForeignCheckout", () => {
  const temporaryDirectory = realpathSync(tmpdir());
  let workspace = "";
  let fixtureDirectory = "";
  let outside = "";

  beforeEach(() => {
    workspace = mkdtempSync(path.join(temporaryDirectory, "rnl-t3code-checkout-"));
    fixtureDirectory = path.join(workspace, "build", "t3code");
    outside = path.join(workspace, "t3code");
    mkdirSync(path.join(fixtureDirectory, "checkout"), { recursive: true });
    mkdirSync(outside);
  });

  afterEach(() => {
    rmSync(workspace, { force: true, recursive: true });
  });

  it("accepts a checkout inside the fixture directory, also through a symlink that stays inside", () => {
    symlinkSync(path.join(fixtureDirectory, "checkout"), path.join(fixtureDirectory, "link"));

    expect(describeForeignCheckout(path.join(fixtureDirectory, "checkout"), fixtureDirectory)).toBeNull();
    expect(describeForeignCheckout(path.join(fixtureDirectory, "link"), fixtureDirectory)).toBeNull();
  });

  it("refuses a checkout outside the fixture directory and says where to clone one", () => {
    expect(describeForeignCheckout(outside, fixtureDirectory)).toBe(
      `${outside} resolves to ${outside}, outside ${fixtureDirectory}. The fixture rewrites the checkout's package.json and lockfile, so clone a copy of T3 Code at ${T3CODE_REVISION} into ${path.join(fixtureDirectory, "checkout")} and pass that.`,
    );
  });

  it("refuses a symlink inside the fixture directory that points outside it", () => {
    const link = path.join(fixtureDirectory, "link");

    symlinkSync(outside, link);

    expect(describeForeignCheckout(link, fixtureDirectory)).toContain(`${link} resolves to ${outside}, outside`);
  });

  it("refuses the fixture directory itself, a sibling whose name only starts like it, and the overlay directory", () => {
    const [sibling, inOverlays] = [`${fixtureDirectory}-copy`, path.join(fixtureDirectory, OVERLAY_DIRECTORY_NAME)];

    mkdirSync(sibling);
    mkdirSync(inOverlays);

    expect(describeForeignCheckout(inOverlays, fixtureDirectory)).toContain("inside the overlay directory");
    expect(describeForeignCheckout(fixtureDirectory, fixtureDirectory)).toContain("outside");
    expect(describeForeignCheckout(sibling, fixtureDirectory)).toContain("outside");
  });
});

describe("describeForeignCheckout around the overlay directory", () => {
  it("refuses a checkout nested in the overlay directory, and a symlink that resolves into it", () => {
    const temporaryDirectory = realpathSync(tmpdir());
    const workspace = mkdtempSync(path.join(temporaryDirectory, "rnl-t3code-overlays-"));
    const fixtureDirectory = path.join(workspace, "build", "t3code");
    const nested = path.join(fixtureDirectory, OVERLAY_DIRECTORY_NAME, "checkout");
    const link = path.join(fixtureDirectory, "checkout");

    mkdirSync(nested, { recursive: true });
    symlinkSync(nested, link);

    expect(describeForeignCheckout(nested, fixtureDirectory)).toContain("inside the overlay directory");
    expect(describeForeignCheckout(link, fixtureDirectory)).toContain("inside the overlay directory");
    rmSync(workspace, { force: true, recursive: true });
  });
});
