import {
  APP_URL_PREFIX,
  buildAppEnvironment,
  describeMissingBuild,
  driveWorkload,
  resolveT3Code,
  seedHome,
  waitForThreadList,
} from "./perf-t3code.ts";
import { STARTUP_TIMEOUT_MS, runApp } from "./perf-session.ts";
import { argv, stderr, stdout } from "node:process";
import { describeMissingCompositor, withCompositor } from "./perf-compositor.ts";
import { formatReport, summarizeRun } from "./perf/report.ts";
import { mkdirSync, writeFileSync } from "node:fs";
import { parseArgs } from "node:util";
import path from "node:path";

/*
 * #642: T3 Code's Electron build on one fixed workload in a private headless cage at a set refresh rate, measured
 * from the compositor's presentation events and /proc. From a T3 Code checkout that has run `vp run build:desktop`:
 *   pnpm perf:electron --t3code <checkout> [--refresh-hz 120]
 * See docs/research/t3code-electron-baseline.md for the method and the checked-in numbers.
 */

const UNAVAILABLE_EXIT_STATUS = 2;
const FIRST_ARGUMENT = 2;
const NO_REASONS = 0;
const NO_RATE = 0;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const { values: options } = parseArgs({
  args: argv.slice(FIRST_ARGUMENT),
  options: { "refresh-hz": { default: "120", type: "string" }, t3code: { type: "string" } },
});
const refreshHz = Number(options["refresh-hz"]);
const t3code = resolveT3Code(path.resolve(options.t3code ?? "."));

const writeArtifacts = (trace: string, report: string): string => {
  const directory = path.join(repositoryRoot, "build", "perf-electron", new Date().toISOString());

  mkdirSync(directory, { recursive: true });
  writeFileSync(path.join(directory, "trace.log"), trace);
  writeFileSync(path.join(directory, "report.md"), report);

  return `${report}\nartifacts: ${directory}\n`;
};

/** T3 Code's window on the fixture, ready once its sidebar lists the fixture's threads. */
const measureElectron = (): Promise<string> =>
  withCompositor(refreshHz, async (display) => {
    const t3Home = path.join(display.scratchDirectory, "home", "t3");
    const removeSeed = seedHome(t3code, t3Home);
    const launch = {
      arguments: [t3code.desktopDirectory],
      environment: buildAppEnvironment(t3Home),
      executablePath: t3code.electronPath,
      pageUrlPrefix: APP_URL_PREFIX,
    };
    const run = await runApp(
      {
        ...launch,
        drive: (page, measure) => driveWorkload({ measure, page, t3Home, t3code }),
        waitForReady: (page) => waitForThreadList(page, STARTUP_TIMEOUT_MS),
      },
      display,
    ).finally(removeSeed);
    const result = summarizeRun(run, refreshHz);

    if (result === null) {
      throw new Error("Electron never bound wp_presentation on CLOCK_MONOTONIC, so no frame was measured");
    }

    return writeArtifacts(run.trace, formatReport(result));
  });

const unavailableReasons = [
  ...(typeof options.t3code === "string"
    ? []
    : ["pass --t3code <a T3 Code checkout that has run vp run build:desktop>"]),
  ...(refreshHz > NO_RATE ? [] : [`--refresh-hz ${options["refresh-hz"]} is not a refresh rate`]),
  ...describeMissingCompositor(),
  ...describeMissingBuild(t3code),
];

if (unavailableReasons.length > NO_REASONS) {
  stderr.write(unavailableReasons.map((reason) => `${reason}\n`).join(""));
  process.exitCode = UNAVAILABLE_EXIT_STATUS;
} else {
  stdout.write(await measureElectron());
}
