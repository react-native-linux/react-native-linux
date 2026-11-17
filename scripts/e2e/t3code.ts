import { FIRST_PRESENTED_FRAME_TRACE_LINE, hasPresentedFirstFrame } from "./window-readiness.ts";
import { MAIN_SCREEN_BUTTON_NAME, findStartupErrors } from "./t3code-startup.ts";
import { cpSync, readdirSync, realpathSync } from "node:fs";
import { gradeScreenshot } from "./grade.ts";
import path from "node:path";
import { spawnSync } from "node:child_process";

/** The T3 Code revision docs/research/t3code-linux.md surveyed, and the one the fixture runs. */
const T3CODE_REVISION = "238678951ac4945969b8203ae2996b5891b6ffce";

const EMPTY_LENGTH = 0;
const MOBILE_PACKAGE_NAME = "@t3tools/mobile";
const PAIRING_URL_PATTERN = /^Pairing URL: (?<pairingUrl>\S+)$/mu;
const SUCCESSFUL_EXIT_STATUS = 0;
const TAIL_LINE_COUNT = 40;
/**
 * The screenshot comparison of T3 Code's main screen: everything above LogBox's toast, whose warning count varies from
 * run to run, with no pixel budget. The golden holds for the CI container image.
 */
const MAIN_SCREEN_SCREENSHOT = {
  crop: { height: 640, left: 0, top: 0, width: 1280 },
  golden: "t3code-main-screen.png",
  maxDifferentPixels: 0,
};

/**
 * What the fixture saw of one window run: the wayland socket, whether T3 Code's main screen showed and what the last
 * accessibility tree named, the trace, and how the compositor ended.
 */
interface WindowRun {
  readonly exitCode: number | null;
  readonly hasSocket: boolean;
  readonly lastNames: readonly string[];
  readonly mainScreenMs: number | null;
  readonly isTraceClosed: boolean;
  readonly signal: string | null;
  readonly trace: string;
}

/** The pairing URL `t3 serve` prints once it is ready, carrying its port and one-time startup token; null until then. */
const readPairingUrl = (serverOutput: string): string | null =>
  PAIRING_URL_PATTERN.exec(serverOutput)?.groups?.["pairingUrl"] ?? null;

/** The link that opens T3 Code's pairing form and connects at once: `features/connection/devAutoConnect.ts`. */
const buildPairingLink = (pairingUrl: string): string =>
  `t3code://connections/new?${new URLSearchParams([
    ["pairingUrl", pairingUrl],
    ["autoConnect", "1"],
  ]).toString()}`;

/** The last lines of a log, which is what a failure prints of it. */
const tailLines = (text: string): string => text.trimEnd().split("\n").slice(-TAIL_LINE_COUNT).join("\n");

const LINUX_VARIANT_PATTERN = /\.linux\.tsx?$/u;

/**
 * The Linux variants of T3 Code's own files (#707) under `variantsDirectory`, copied beside the files they replace in
 * `mobileRoot/src` as T3 Code would carry them, so Metro picks each by its platform extension and its relative imports
 * reach T3 Code's own modules. The directory's other files are stand-ins for T3 Code's files that only its test bundle
 * uses, and stay behind. Answers why nothing was copied, or null: a destination T3 Code's git tracks is a variant of
 * its own, which a copy would replace without a trace.
 */
const installVariants = (variantsDirectory: string, mobileRoot: string): string | null => {
  const destinations = readdirSync(variantsDirectory, { encoding: "utf8", recursive: true })
    .filter((relativePath) => LINUX_VARIANT_PATTERN.test(relativePath))
    .map((variant) => path.join("src", variant));
  const tracked = spawnSync("git", ["-C", mobileRoot, "ls-files", "--", ...destinations], { encoding: "utf8" });

  if (tracked.status !== SUCCESSFUL_EXIT_STATUS || tracked.stdout !== "") {
    return `T3 Code tracks a Linux variant of its own, or git could not tell, so none was copied into ${mobileRoot}\n${tracked.stdout}${tracked.stderr}`;
  }

  for (const destination of destinations) {
    cpSync(path.join(variantsDirectory, path.relative("src", destination)), path.join(mobileRoot, destination));
  }

  return null;
};

const describeLastNames = (names: readonly string[]): string =>
  names.length === EMPTY_LENGTH ? "nothing" : names.map((name) => JSON.stringify(name)).join(", ");

/**
 * Why a run is not a pass, the errors startup reported leading as where it stopped; empty only when the socket came
 * up, the window presented its first frame, T3 Code showed its main screen, and the compositor exited successfully.
 */
const gradeWindowRun = (run: WindowRun): readonly string[] => {
  const startupErrors = findStartupErrors(run.trace);

  return [
    ...(startupErrors.length === EMPTY_LENGTH ? [] : [`startup reported\n${startupErrors.join("\n")}`]),
    ...(run.hasSocket ? [] : ["the compositor never created a wayland socket"]),
    ...(hasPresentedFirstFrame(run.trace) ? [] : [`the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`]),
    ...(run.mainScreenMs === null
      ? [
          `T3 Code's main screen never showed its "${MAIN_SCREEN_BUTTON_NAME}" button; the last accessibility tree named ${describeLastNames(run.lastNames)}`,
        ]
      : []),
    ...(run.isTraceClosed ? [] : ["the window did not exit within the run timeout"]),
    ...(run.exitCode === SUCCESSFUL_EXIT_STATUS
      ? []
      : [`the compositor ended with exit code ${String(run.exitCode)}, signal ${String(run.signal)}`]),
  ];
};

/**
 * Why a run is not a pass, and what it notes: `gradeWindowRun`'s failures, and once there are none, the screenshot of
 * T3 Code's main screen against its golden in `goldensDirectory`, which only notes a golden not blessed yet.
 */
const gradeMainScreenRun = (
  run: WindowRun,
  goldensDirectory: string,
  screenshotPath: string,
): ReturnType<typeof gradeScreenshot> => {
  const failures = gradeWindowRun(run);

  return failures.length === EMPTY_LENGTH
    ? gradeScreenshot(MAIN_SCREEN_SCREENSHOT, goldensDirectory, screenshotPath)
    : { failures, notes: [] };
};

/** The pnpm arguments that pack each overlay package the platform's Metro alias table names into `destination`. */
const overlayPackArguments = (
  aliases: readonly { readonly linux: string }[],
  destination: string,
): readonly string[] => [
  ...aliases.flatMap(({ linux }) => ["--filter", linux]),
  "pack",
  "--pack-destination",
  destination,
];

/**
 * Why the fixture refuses to modify `checkout`, or null when its real path lies inside `fixtureDirectory`. Adding the
 * overlays rewrites the checkout's package.json and lockfile, so it must be the fixture's own copy of T3 Code.
 */
/** The directory under the fixture directory that receives the packed overlays, emptied before each run. */
const OVERLAY_DIRECTORY_NAME = "overlays";

const describeForeignCheckout = (checkout: string, fixtureDirectory: string): string | null => {
  const realFixtureDirectory = realpathSync(fixtureDirectory);
  const realCheckout = realpathSync(checkout);
  const relativeCheckout = path.relative(realFixtureDirectory, realCheckout);

  if (`${relativeCheckout}${path.sep}`.startsWith(`${OVERLAY_DIRECTORY_NAME}${path.sep}`)) {
    return `${checkout} resolves to ${realCheckout}, inside the overlay directory the fixture empties before each run. Clone T3 Code into ${path.join(fixtureDirectory, "checkout")} and pass that.`;
  }

  return relativeCheckout !== "" && !relativeCheckout.startsWith("..")
    ? null
    : `${checkout} resolves to ${realCheckout}, outside ${realFixtureDirectory}. The fixture rewrites the checkout's package.json and lockfile, so clone a copy of T3 Code at ${T3CODE_REVISION} into ${path.join(fixtureDirectory, "checkout")} and pass that.`;
};

/**
 * The pnpm arguments that install a T3 Code checkout for the fixture: the mobile app, the server, and the workspace
 * packages each depends on. They also run after the overlays are added, because an add that cannot hard link from the
 * store the checkout was installed with (a container, a copied checkout) relinks node_modules for the mobile app alone
 * and leaves the links of its workspace dependencies, such as packages/shared's remark-math, pointing at nothing.
 */
const CHECKOUT_INSTALL_ARGUMENTS: readonly string[] = [
  "install",
  "--frozen-lockfile",
  "--ignore-scripts",
  "--filter",
  `${MOBILE_PACKAGE_NAME}...`,
  "--filter",
  "t3...",
];

/**
 * The pnpm arguments that add the packed overlays to T3 Code's mobile app as a registry install would land them. Their
 * install scripts do not run, as the checkout's own install skips them.
 */
const overlayInstallArguments = (tarballs: readonly string[]): readonly string[] => [
  "--filter",
  MOBILE_PACKAGE_NAME,
  "add",
  ...tarballs,
  "--ignore-scripts",
];

export {
  CHECKOUT_INSTALL_ARGUMENTS,
  T3CODE_REVISION,
  gradeMainScreenRun,
  buildPairingLink,
  OVERLAY_DIRECTORY_NAME,
  describeForeignCheckout,
  gradeWindowRun,
  installVariants,
  overlayInstallArguments,
  overlayPackArguments,
  readPairingUrl,
  tailLines,
};
