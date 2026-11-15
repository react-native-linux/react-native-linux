/*
 * CPU time and memory of an Electron app's process tree, from /proc. The tree splits three ways: the GPU process
 * (in a software rung it rasterises on the CPU), the T3 backend (the process whose arguments hold the backend's
 * entry script, plus everything under it), and the rest of the client, which includes helpers the main process
 * starts in Node mode for other jobs.
 */

/** Fields after the `)` that closes `comm` in /proc/<pid>/stat: state is 0, ppid 1, utime 11, stime 12. */
const PARENT_PID_FIELD = 1;
const USER_TICKS_FIELD = 11;
const SYSTEM_TICKS_FIELD = 12;
const CLOSING_PARENTHESIS_LENGTH = 1;
const kilobytesPattern = (name: string): RegExp => new RegExp(`^${name}:\\s+(?<kilobytes>\\d+) kB$`, "mu");
const NO_TICKS = 0;
const PROCESS_DIRECTORY_PATTERN = /^\d+$/u;
const NO_KILOBYTES = 0;

type ProcessRole = "backend" | "client" | "gpu";

interface ProcessSample {
  /** /proc/<pid>/cmdline split at its NUL separators, so an argument that holds a space stays whole. */
  readonly commandArguments: readonly string[];
  readonly cpuTicks: number;
  readonly parentPid: number;
  readonly pid: number;
  readonly pssKilobytes: number;
  readonly rssKilobytes: number;
}

interface RoleUsage {
  readonly cpuTicks: number;
  readonly pssKilobytes: number;
  readonly rssKilobytes: number;
}

type TreeUsage = Readonly<Record<ProcessRole, RoleUsage>>;

const parseProcessStat = (statText: string): { cpuTicks: number; parentPid: number } | null => {
  const fields = statText
    .slice(statText.lastIndexOf(")") + CLOSING_PARENTHESIS_LENGTH)
    .trim()
    .split(" ");
  const [parentPid, userTicks, systemTicks] = [PARENT_PID_FIELD, USER_TICKS_FIELD, SYSTEM_TICKS_FIELD].map((field) =>
    Number(fields[field]),
  );

  return Number.isInteger(parentPid) && Number.isInteger(userTicks) && Number.isInteger(systemTicks)
    ? { cpuTicks: Number(userTicks) + Number(systemTicks), parentPid: Number(parentPid) }
    : null;
};

const readKilobytes = (smapsRollupText: string, name: string): number =>
  Number(kilobytesPattern(name).exec(smapsRollupText)?.groups?.["kilobytes"] ?? NO_KILOBYTES);

const parseSmapsRollup = (smapsRollupText: string): { pssKilobytes: number; rssKilobytes: number } => ({
  pssKilobytes: readKilobytes(smapsRollupText, "Pss"),
  rssKilobytes: readKilobytes(smapsRollupText, "Rss"),
});

const readProcessSample = (pid: string, readText: (procPath: string) => string): readonly ProcessSample[] => {
  try {
    const stat = parseProcessStat(readText(`/proc/${pid}/stat`));
    const memory = parseSmapsRollup(readText(`/proc/${pid}/smaps_rollup`));
    const commandArguments = readText(`/proc/${pid}/cmdline`)
      .split("\0")
      .filter((argument) => argument !== "");

    return stat === null ? [] : [{ ...stat, ...memory, commandArguments, pid: Number(pid) }];
  } catch {
    // The process exited between the directory listing and the read.
    return [];
  }
};

/** Every process listed in /proc that could still be read; `readText` is `readFileSync` outside the tests. */
const readProcessSamples = (
  procEntries: readonly string[],
  readText: (procPath: string) => string,
): readonly ProcessSample[] =>
  procEntries
    .filter((entry) => PROCESS_DIRECTORY_PATTERN.test(entry))
    .flatMap((entry) => readProcessSample(entry, readText));

/** A tree by its root, and where the backend's entry script is, or null for a tree with no backend. */
interface ProcessTree {
  readonly backendEntryPath: string | null;
  readonly rootPid: number;
}

const resolveRole = (
  sample: ProcessSample,
  parentRole: ProcessRole | null,
  backendEntryPath: string | null,
): ProcessRole => {
  if (parentRole === "backend" || (backendEntryPath !== null && sample.commandArguments.includes(backendEntryPath))) {
    return "backend";
  }

  return sample.commandArguments.includes("--type=gpu-process") ? "gpu" : "client";
};

/** Every process in the tree with its role; processes outside the tree are left out. */
const classifyProcessTree = (
  samples: readonly ProcessSample[],
  { backendEntryPath, rootPid }: ProcessTree,
): ReadonlyMap<number, ProcessRole> => {
  const roles = new Map<number, ProcessRole>();
  const visit = (sample: ProcessSample, parentRole: ProcessRole | null): void => {
    const role = resolveRole(sample, parentRole, backendEntryPath);

    roles.set(sample.pid, role);
    for (const child of samples.filter((candidate) => candidate.parentPid === sample.pid)) {
      visit(child, role);
    }
  };

  for (const root of samples.filter((sample) => sample.pid === rootPid)) {
    visit(root, null);
  }

  return roles;
};

/**
 * CPU ticks spent between the two samples and memory at the second, per role. A process born in between counts all
 * its ticks; one that exited in between is missing from `after`, so its last ticks are lost and the figure is a floor.
 */
const summarizeTreeUsage = (
  before: readonly ProcessSample[],
  after: readonly ProcessSample[],
  tree: ProcessTree,
): TreeUsage => {
  const roles = classifyProcessTree(after, tree);
  const ticksBefore = new Map(before.map((sample) => [sample.pid, sample.cpuTicks]));
  const usage = {
    backend: { cpuTicks: NO_TICKS, pssKilobytes: NO_KILOBYTES, rssKilobytes: NO_KILOBYTES },
    client: { cpuTicks: NO_TICKS, pssKilobytes: NO_KILOBYTES, rssKilobytes: NO_KILOBYTES },
    gpu: { cpuTicks: NO_TICKS, pssKilobytes: NO_KILOBYTES, rssKilobytes: NO_KILOBYTES },
  };

  for (const sample of after) {
    const role = roles.get(sample.pid) ?? null;

    if (role !== null) {
      usage[role] = {
        cpuTicks: usage[role].cpuTicks + sample.cpuTicks - (ticksBefore.get(sample.pid) ?? NO_TICKS),
        pssKilobytes: usage[role].pssKilobytes + sample.pssKilobytes,
        rssKilobytes: usage[role].rssKilobytes + sample.rssKilobytes,
      };
    }
  }

  return usage;
};

export { classifyProcessTree, readProcessSamples, summarizeTreeUsage };
export type { TreeUsage };
