import { classifyProcessTree, readProcessSamples, summarizeTreeUsage } from "./processes.ts";
import { describe, expect, it } from "vitest";

const ELECTRON = "/opt/t3/electron";
const BACKEND_ENTRY = "/opt/t3/apps/server/dist/bin.mjs";
const ROOT_PID = 27;
const NOT_RUNNING_PID = 9999;
const DEFAULT_CPU_TICKS = 10;
const APP_TREE = { backendEntryPath: BACKEND_ENTRY, rootPid: ROOT_PID };
const ONE_SAMPLE = 1;
const EMPTY = 0;

interface StatFields {
  readonly parentPid: number;
  readonly systemTicks: number;
  readonly userTicks: number;
}

/** /proc/<pid>/stat with a `comm` that holds a space and a parenthesis, as Chromium's thread names can. */
const statText = ({ parentPid, systemTicks, userTicks }: StatFields): string =>
  `${String(ROOT_PID)} (Chrome (child) x) S ${String(parentPid)} 25 1 0 -1 4194560 100 0 0 0 ` +
  `${String(userTicks)} ${String(systemTicks)} 0 0 20 0 30`;

const procFiles = new Map([
  ["/proc/27/stat", statText({ parentPid: 1, systemTicks: 200, userTicks: 300 })],
  ["/proc/27/smaps_rollup", "55d0-7ffe ---p 00000000 00:00 0 [rollup]\nRss:  250000 kB\nPss:  120000 kB\n"],
  ["/proc/27/cmdline", `${ELECTRON}\0/opt/t3/apps/desktop\0--no-sandbox\0`],
  ["/proc/28/stat", statText({ parentPid: 2, systemTicks: 0, userTicks: 0 })],
  ["/proc/28/smaps_rollup", ""],
  ["/proc/28/cmdline", ""],
]);

const readText = (procPath: string): string => {
  const text = procFiles.get(procPath) ?? null;

  if (text === null) {
    throw new Error(`ENOENT: ${procPath}`);
  }

  return text;
};

interface SampleFields {
  readonly commandLine: string;
  readonly cpuTicks?: number;
  readonly parentPid: number;
  readonly pid: number;
}

const sample = ({
  commandLine,
  cpuTicks = DEFAULT_CPU_TICKS,
  parentPid,
  pid,
}: SampleFields): ReturnType<typeof readProcessSamples>[number] => ({
  commandArguments: commandLine.split(" ").filter((argument) => argument !== ""),
  cpuTicks,
  parentPid,
  pid,
  pssKilobytes: 100,
  rssKilobytes: 200,
});

/**
 * The tree the Electron build runs as: main, zygote, GPU, renderer, the backend in Node mode and its agent, and a
 * helper the main process also starts in Node mode, which is part of the client, not the backend.
 */
const tree = [
  sample({ commandLine: `${ELECTRON} /opt/t3/apps/desktop --no-sandbox`, parentPid: 1, pid: ROOT_PID }),
  sample({ commandLine: `${ELECTRON} --type=zygote --no-zygote-sandbox`, parentPid: ROOT_PID, pid: 29 }),
  sample({ commandLine: `${ELECTRON} --type=gpu-process --ozone-platform=wayland`, parentPid: 29, pid: 61 }),
  sample({ commandLine: `${ELECTRON} --type=renderer --enable-crash-reporter`, parentPid: 29, pid: 93 }),
  sample({ commandLine: `${ELECTRON} ${BACKEND_ENTRY} --bootstrap-fd 3 `, parentPid: ROOT_PID, pid: 138 }),
  sample({ commandLine: "node dev-replay-agent.ts --type=renderer", parentPid: 138, pid: 170 }),
  sample({ commandLine: `${ELECTRON} /opt/t3/apps/desktop/shortcut-helper.js `, parentPid: ROOT_PID, pid: 201 }),
  sample({ commandLine: "git status --porcelain", parentPid: 201, pid: 202 }),
  sample({ commandLine: "bash", parentPid: 1, pid: 500 }),
];

describe("readProcessSamples", () => {
  it("reads CPU ticks, the parent, memory and the command line of each numeric /proc entry", () => {
    expect(readProcessSamples(["27", "self", "cpuinfo"], readText)).toEqual([
      {
        commandArguments: [ELECTRON, "/opt/t3/apps/desktop", "--no-sandbox"],
        cpuTicks: 500,
        parentPid: 1,
        pid: 27,
        pssKilobytes: 120_000,
        rssKilobytes: 250_000,
      },
    ]);
  });

  it("skips a process that exited between the listing and the read", () => {
    expect(readProcessSamples(["27", "4242"], readText)).toHaveLength(ONE_SAMPLE);
  });

  it("reads missing memory lines as zero, as for a kernel thread", () => {
    expect(readProcessSamples(["28"], readText)).toEqual([
      { commandArguments: [], cpuTicks: 0, parentPid: 2, pid: 28, pssKilobytes: 0, rssKilobytes: 0 },
    ]);
  });

  it.each([
    ["no fields after comm", "30 (electron)"],
    ["a stat cut before utime", "30 (electron) S 1 25 1 0 -1 4194560 100 0 0 0"],
    ["a stat cut before stime", "30 (electron) S 1 25 1 0 -1 4194560 100 0 0 0 300"],
  ])("skips a process whose stat has %s", (_reason, text) => {
    const readCutStat = (procPath: string): string => (procPath.endsWith("/stat") ? text : "");

    expect(readProcessSamples(["30"], readCutStat)).toEqual([]);
  });
});

describe("classifyProcessTree", () => {
  it("splits the tree into client, GPU process and backend, only the backend's own children inheriting it", () => {
    expect(Object.fromEntries(classifyProcessTree(tree, APP_TREE))).toEqual({
      138: "backend",
      170: "backend",
      201: "client",
      202: "client",
      27: "client",
      29: "client",
      61: "gpu",
      93: "client",
    });
  });

  it("matches the backend's entry script read from /proc as one argument when the checkout path holds spaces", () => {
    const spacedEntry = "/home/user/My Projects/t3/apps/server/dist/bin.mjs";
    const spacedFiles = new Map([
      ["/proc/27/stat", statText({ parentPid: 1, systemTicks: 0, userTicks: 0 })],
      ["/proc/27/cmdline", [ELECTRON, "/home/user/My Projects/t3/apps/desktop", ""].join("\0")],
      ["/proc/138/stat", statText({ parentPid: ROOT_PID, systemTicks: 0, userTicks: 0 })],
      ["/proc/138/cmdline", [ELECTRON, spacedEntry, "--bootstrap-fd", "3", ""].join("\0")],
    ]);
    const samples = readProcessSamples(["27", "138"], (procPath) => spacedFiles.get(procPath) ?? "");

    expect(Object.fromEntries(classifyProcessTree(samples, { ...APP_TREE, backendEntryPath: spacedEntry }))).toEqual({
      138: "backend",
      27: "client",
    });
  });

  it("finds no backend in a tree that has none, even where an argument is empty", () => {
    expect([...classifyProcessTree(tree, { backendEntryPath: null, rootPid: ROOT_PID }).values()]).not.toContain(
      "backend",
    );
  });

  it("is empty when the root is not running", () => {
    expect(classifyProcessTree(tree, { ...APP_TREE, rootPid: NOT_RUNNING_PID }).size).toBe(EMPTY);
  });
});

describe("summarizeTreeUsage", () => {
  it("sums CPU ticks spent since the first sample and memory at the second, per role", () => {
    const before = [
      sample({ commandLine: "", cpuTicks: 4, parentPid: 1, pid: ROOT_PID }),
      sample({ commandLine: "", cpuTicks: 7, parentPid: 29, pid: 61 }),
    ];

    expect(summarizeTreeUsage(before, tree, APP_TREE)).toEqual({
      backend: { cpuTicks: 20, pssKilobytes: 200, rssKilobytes: 400 },
      client: { cpuTicks: 46, pssKilobytes: 500, rssKilobytes: 1000 },
      gpu: { cpuTicks: 3, pssKilobytes: 100, rssKilobytes: 200 },
    });
  });
});
