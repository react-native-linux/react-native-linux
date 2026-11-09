/*
 * The counter probe of #76. `rnl_window` prints an `[rnl-teardown]` line between an unloaded instance and the next
 * one, naming the GPU resources Skia still holds; a scenario with `steadyAcrossReloads` also runs it under
 * `WAYLAND_DEBUG=client`, whose request log interleaves with that line in write order (#512). Replaying the log up
 * to each probe gives the live client-created Wayland objects at that moment, and every probe must find the same
 * state as the first: a lifetime bug only shows up on the second instance.
 */

const PROBE_TAG = "[rnl-teardown]";
const REQUEST_MARKER = "-> ";
const DELETE_ID_PATTERN = /wl_display@1\.delete_id\((?<id>\d+)\)/u;
const NEW_ID_PATTERN = /new id (?<interfaceName>[^@\s]+)@(?<id>\d+)/u;
const BIND_INTERFACE_PATTERN = /\.bind\(\d+, "(?<interfaceName>[^"]+)"/u;
const DESTRUCTOR_PATTERN = /@(?<id>\d+)\.(?:destroy|release)\(\)/u;
const NO_OBJECTS = 0;
const ONE_OBJECT = 1;
const FIRST_RELOAD_NUMBER = 1;
const MINIMUM_PROBE_COUNT = 2;

/**
 * Objects a frame creates and the compositor retires on its own schedule: how many are in flight at a probe is a
 * matter of frame timing, not of what an unload left behind.
 */
const PER_FRAME_INTERFACES = new Set(["wl_callback", "wp_presentation_feedback"]);

interface Probe {
  readonly line: string;
  readonly waylandObjects: string;
}

const readId = (pattern: RegExp, line: string): number | null => {
  const id = pattern.exec(line)?.groups?.["id"] ?? null;

  return id === null ? null : Number(id);
};

/** A bind's new id prints as `[unknown]`; its interface is the quoted argument before it. */
const readNewObject = (line: string): readonly [number, string] | null => {
  const created = NEW_ID_PATTERN.exec(line)?.groups ?? null;

  if (created === null) {
    return null;
  }

  const interfaceName = BIND_INTERFACE_PATTERN.exec(line)?.groups?.["interfaceName"] ?? created["interfaceName"];

  return [Number(created["id"]), String(interfaceName)];
};

const replayRequest = (line: string, liveObjects: Map<number, string>): void => {
  const destroyedId = readId(DESTRUCTOR_PATTERN, line);
  const created = readNewObject(line);

  if (destroyedId !== null) {
    liveObjects.delete(destroyedId);
  }

  if (created !== null) {
    liveObjects.set(...created);
  }
};

const replayLine = (line: string, liveObjects: Map<number, string>): void => {
  const deletedId = readId(DELETE_ID_PATTERN, line);

  if (deletedId !== null) {
    liveObjects.delete(deletedId);
  } else if (line.includes(REQUEST_MARKER)) {
    replayRequest(line, liveObjects);
  }
};

/** `wl_surface×2 xdg_surface×1 …`, sorted, so two probes compare as strings and a difference reads as a diff. */
const describeLiveObjects = (liveObjects: ReadonlyMap<number, string>): string => {
  const counts = new Map<string, number>();

  for (const interfaceName of liveObjects.values()) {
    if (!PER_FRAME_INTERFACES.has(interfaceName)) {
      counts.set(interfaceName, (counts.get(interfaceName) ?? NO_OBJECTS) + ONE_OBJECT);
    }
  }

  return [...counts]
    .toSorted(([left], [right]) => left.localeCompare(right))
    .map(([interfaceName, count]) => `${interfaceName}×${String(count)}`)
    .join(" ");
};

const collectProbes = (traceLines: readonly string[]): readonly Probe[] => {
  const liveObjects = new Map<number, string>();
  const probes: Probe[] = [];

  for (const line of traceLines) {
    replayLine(line, liveObjects);

    if (line.includes(PROBE_TAG)) {
      probes.push({ line: line.slice(line.indexOf(PROBE_TAG)), waylandObjects: describeLiveObjects(liveObjects) });
    }
  }

  return probes;
};

const describeProbe = (probe: Probe): string => `"${probe.line}" with Wayland objects ${probe.waylandObjects}`;

const describeReloadCounterFailures = (traceLines: readonly string[]): readonly string[] => {
  const probes = collectProbes(traceLines);
  const [first = null] = probes;

  if (first === null || probes.length < MINIMUM_PROBE_COUNT) {
    return [`steadyAcrossReloads needs at least ${String(MINIMUM_PROBE_COUNT)} "${PROBE_TAG}" lines`];
  }

  return probes.flatMap((probe, index) =>
    probe.line === first.line && probe.waylandObjects === first.waylandObjects
      ? []
      : [
          `reload ${String(index + FIRST_RELOAD_NUMBER)} did not return to reload 1's state: ` +
            `${describeProbe(probe)}, versus ${describeProbe(first)}`,
        ],
  );
};

export { describeReloadCounterFailures };
