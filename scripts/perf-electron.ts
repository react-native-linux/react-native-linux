import { argv, env, stderr, stdout } from "node:process";
import { describeMissingBuild, resolveT3Code, seedHome } from "./perf-t3code.ts";
import { describeMissingCompositor, withCompositor } from "./perf-compositor.ts";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { formatReport, summarizeRun } from "./perf/report.ts";
import { injectorBinaryPath } from "./e2e-processes.ts";
import { parseArgs } from "node:util";
import path from "node:path";
import { runApp } from "./perf-session.ts";

/*
 * #642: T3 Code's Electron build on one fixed workload in a private headless cage at a set refresh rate, measured
 * from the compositor's presentation events and /proc. From a T3 Code checkout that has run `vp run build:desktop`,
 * with rnl_inject built in build/dev:
 *   pnpm perf:electron --t3code <checkout> [--refresh-hz 120] [--renderer software|llvmpipe|gpu]
 *     [--electron-arguments="<more Chromium switches>"]
 * See docs/research/t3code-electron-baseline.md for the method and the checked-in numbers.
 */

const UNAVAILABLE_EXIT_STATUS = 2;
const FIRST_ARGUMENT = 2;
const NO_REASONS = 0;
const NO_RATE = 0;
const NO_ARGUMENTS = 0;
const CPUS_ALLOWED_PATTERN = /^Cpus_allowed_list:\s*(?<cpus>\S+)$/mu;

/** Each rung pins how Electron draws and names the rnl_window `--renderer` rung it is compared with. */
const RUNGS = {
  /** Chromium on the machine's GPU, composited by wlroots on its render node; rnl_window's `preferred-vulkan`. */
  gpu: { compositor: "gles2", electronArguments: [], rnlWindow: "preferred-vulkan" },
  /**
   * Chromium's GPU path on Mesa's llvmpipe, its blocklist overridden; rnl_window's `software-vulkan` (lavapipe).
   * Only where no GPU is visible: with a render node in reach ANGLE takes the GPU, as the printed renderer shows.
   */
  llvmpipe: { compositor: "pixman", electronArguments: ["--ignore-gpu-blocklist"], rnlWindow: "software-vulkan" },
  /** Chromium's software raster and compositor, its path wherever it blocklists the GPU; rnl_window's `raster`. */
  software: { compositor: "pixman", electronArguments: ["--disable-gpu"], rnlWindow: "raster" },
} as const;

const repositoryRoot = path.resolve(import.meta.dirname, "..");
const { values: options } = parseArgs({
  args: argv.slice(FIRST_ARGUMENT),
  options: {
    "electron-arguments": { default: "", type: "string" },
    "refresh-hz": { default: "120", type: "string" },
    renderer: { default: "software", type: "string" },
    t3code: { type: "string" },
  },
});
const refreshHz = Number(options["refresh-hz"]);
const t3code = resolveT3Code(path.resolve(options.t3code ?? "."));
const rungName = options.renderer;
const extraArguments = options["electron-arguments"].split(" ").filter((argument) => argument !== "");
const isRung = (name: string): name is keyof typeof RUNGS => Object.hasOwn(RUNGS, name);

const writeArtifacts = (trace: string, report: string): string => {
  const directory = path.join(repositoryRoot, "build", "perf-electron", new Date().toISOString());

  mkdirSync(directory, { recursive: true });
  writeFileSync(path.join(directory, "trace.log"), trace);
  writeFileSync(path.join(directory, "report.md"), report);

  return `${report}\nartifacts: ${directory}\n`;
};

/** The rung, and what else on the machine shapes a software run: its load, the CPUs it may use, llvmpipe's threads. */
const describeConditions = (electronArguments: readonly string[], rung: (typeof RUNGS)[keyof typeof RUNGS]): string => {
  const [load] = readFileSync("/proc/loadavg", "utf8").split(" ");
  const cpus = CPUS_ALLOWED_PATTERN.exec(readFileSync("/proc/self/status", "utf8"))?.groups?.["cpus"] ?? "unknown";
  const flags = electronArguments.length > NO_ARGUMENTS ? electronArguments.join(" ") : "no extra switches";

  return (
    `Rung ${rungName}: Electron with ${flags}, cage on ${rung.compositor}; compare with rnl_window ` +
    `--renderer ${rung.rnlWindow}. Load at start ${String(load)}, CPUs ${cpus}, ` +
    `LP_NUM_THREADS ${env["LP_NUM_THREADS"] ?? "unset"}.`
  );
};

const measureElectron = (rung: (typeof RUNGS)[keyof typeof RUNGS]): Promise<string> =>
  withCompositor({ refreshHz, renderer: rung.compositor }, async (display) => {
    const electronArguments = [...rung.electronArguments, ...extraArguments];
    const conditions = describeConditions(electronArguments, rung);
    const t3Home = path.join(display.scratchDirectory, "home", "t3");
    const removeSeed = seedHome(t3code, t3Home);
    const run = await runApp({
      display,
      electronArguments,
      t3Home,
      t3code,
    }).finally(removeSeed);
    const result = summarizeRun(run, refreshHz);

    if (result === null) {
      throw new Error("Electron never bound wp_presentation on CLOCK_MONOTONIC, so no frame was measured");
    }

    return writeArtifacts(run.trace, `${conditions}\n${formatReport(result)}`);
  });

const unavailableReasons = [
  ...(typeof options.t3code === "string"
    ? []
    : ["pass --t3code <a T3 Code checkout that has run vp run build:desktop>"]),
  ...(refreshHz > NO_RATE ? [] : [`--refresh-hz ${options["refresh-hz"]} is not a refresh rate`]),
  ...(isRung(rungName) ? [] : [`--renderer ${rungName} is not one of ${Object.keys(RUNGS).join(", ")}`]),
  ...(existsSync(injectorBinaryPath) ? [] : [`${injectorBinaryPath} is missing; build rnl_inject in build/dev`]),
  ...describeMissingCompositor(),
  ...describeMissingBuild(t3code),
];

if (unavailableReasons.length > NO_REASONS || !isRung(rungName)) {
  stderr.write(unavailableReasons.map((reason) => `${reason}\n`).join(""));
  process.exitCode = UNAVAILABLE_EXIT_STATUS;
} else {
  stdout.write(await measureElectron(RUNGS[rungName]));
}
