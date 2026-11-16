import { FIRST_PRESENTED_FRAME_TRACE_LINE, hasPresentedFirstFrame } from "./window-readiness.ts";
import path from "node:path";
import { realpathSync } from "node:fs";

/** The T3 Code revision docs/research/t3code-linux.md surveyed, and the one the fixture runs. */
const T3CODE_REVISION = "238678951ac4945969b8203ae2996b5891b6ffce";

const FATAL_ERROR_PREFIX = "[js-error] fatal";
const MOBILE_PACKAGE_NAME = "@t3tools/mobile";
const PAIRING_URL_PATTERN = /^Pairing URL: (?<pairingUrl>\S+)$/mu;
const SUCCESSFUL_EXIT_STATUS = 0;
const TAIL_LINE_COUNT = 40;

/** What the fixture saw of one window run: the wayland socket, the trace, and how the compositor ended. */
interface WindowRun {
  readonly exitCode: number | null;
  readonly hasSocket: boolean;
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
 * Whether a file of a module's `src` is a Linux variant T3 Code carries beside the file it replaces, as opposed to a
 * stand-in for one of T3 Code's own files that only the module's test bundle uses.
 */
const isLinuxVariant = (relativePath: string): boolean => LINUX_VARIANT_PATTERN.test(relativePath);

/**
 * Why a run is not a pass, the first fatal JavaScript error leading as where startup stopped; empty only when the
 * socket came up, the window presented its first frame, and the compositor exited successfully within the run.
 */
const gradeWindowRun = (run: WindowRun): readonly string[] => {
  const [firstFatalError = null] = run.trace.split("\n").filter((line) => line.startsWith(FATAL_ERROR_PREFIX));

  return [
    ...(firstFatalError === null ? [] : [`startup stopped at\n${firstFatalError}`]),
    ...(run.hasSocket ? [] : ["the compositor never created a wayland socket"]),
    ...(hasPresentedFirstFrame(run.trace) ? [] : [`the window never printed "${FIRST_PRESENTED_FRAME_TRACE_LINE}"`]),
    ...(run.isTraceClosed ? [] : ["the window did not exit within the run timeout"]),
    ...(run.exitCode === SUCCESSFUL_EXIT_STATUS
      ? []
      : [`the compositor ended with exit code ${String(run.exitCode)}, signal ${String(run.signal)}`]),
  ];
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
  T3CODE_REVISION,
  buildPairingLink,
  OVERLAY_DIRECTORY_NAME,
  describeForeignCheckout,
  gradeWindowRun,
  isLinuxVariant,
  overlayInstallArguments,
  overlayPackArguments,
  readPairingUrl,
  tailLines,
};
