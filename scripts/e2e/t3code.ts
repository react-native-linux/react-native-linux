/** The T3 Code revision docs/research/t3code-linux.md surveyed, and the one the fixture runs. */
const T3CODE_REVISION = "238678951ac4945969b8203ae2996b5891b6ffce";

const FATAL_ERROR_PREFIX = "[js-error] fatal";
const PAIRING_URL_PATTERN = /^Pairing URL: (?<pairingUrl>\S+)$/mu;

/** The pairing URL `t3 serve` prints once it is ready, carrying its one-time startup token; null until then. */
const readPairingUrl = (serverOutput: string): string | null =>
  PAIRING_URL_PATTERN.exec(serverOutput)?.groups?.["pairingUrl"] ?? null;

/** The link that opens T3 Code's pairing form and connects at once: `features/connection/devAutoConnect.ts`. */
const buildPairingLink = (pairingUrl: string): string =>
  `t3code://connections/new?${new URLSearchParams([
    ["pairingUrl", pairingUrl],
    ["autoConnect", "1"],
  ]).toString()}`;

/** The fatal JavaScript errors a run's trace carries, the first of which is where the startup stopped. */
const findFatalErrors = (trace: string): readonly string[] =>
  trace.split("\n").filter((line) => line.startsWith(FATAL_ERROR_PREFIX));

export { T3CODE_REVISION, buildPairingLink, findFatalErrors, readPairingUrl };
